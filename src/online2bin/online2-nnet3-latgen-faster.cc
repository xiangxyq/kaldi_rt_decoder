// online2bin/online2-wav-nnet3-latgen-faster.cc

// Copyright 2014  Johns Hopkins University (author: Daniel Povey)
//           2016  Api.ai (Author: Ilya Platonov)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "online2/online-audio-source.h"
#include "online2/online-nnet3-decoding.h"
#include "online2/online-nnet2-feature-pipeline.h"
#include "online2/onlinebin-util.h"
#include "online2/online-endpoint.h"
#include "fstext/fstext-lib.h"
#include "lat/lattice-functions.h"
#include "util/kaldi-thread.h"
#include "nnet3/nnet-utils.h"

namespace kaldi {

std::string GetDiagnosticsAndPrintOutput(const fst::SymbolTable *word_syms,
                                         const CompactLattice &clat) {
  if (clat.NumStates() == 0) {
    KALDI_WARN << "Empty lattice.";
    return "";
  }
  CompactLattice best_path_clat;
  CompactLatticeShortestPath(clat, &best_path_clat);

  Lattice best_path_lat;
  ConvertLattice(best_path_clat, &best_path_lat);

  LatticeWeight weight;
  std::vector<int32> alignment;
  std::vector<int32> words;
  GetLinearSymbolSequence(best_path_lat, &alignment, &words, &weight);

  std::string results;
  if (word_syms != NULL) {
    for (size_t i = 0; i < words.size(); i++) {
      std::string s = word_syms->Find(words[i]);
      if (s == "")
        KALDI_ERR << "Word-id " << words[i] << " not in symbol table.";
      results += s;
    }
  }

  return results;
}

}

int main(int argc, char *argv[]) {
  using namespace kaldi;
  using namespace fst;

  const char *usage =
      "Reads in wav file(s) and simulates online decoding with neural nets\n"
      "(nnet3 setup), with optional iVector-based speaker adaptation and\n"
      "optional endpointing.  Note: some configuration values and inputs are\n"
      "set via config files whose filenames are passed as options\n"
      "\n"
      "Usage: online2-wav-nnet3-latgen-faster [options] <nnet3-in> <fst-in> \n";

  ParseOptions po(usage);

  std::string word_syms_rxfilename;

  // feature_opts includes configuration for the iVector adaptation,
  // as well as the basic features.
  OnlineNnet2FeaturePipelineConfig feature_opts;
  nnet3::NnetSimpleLoopedComputationOptions decodable_opts;
  LatticeFasterDecoderConfig decoder_opts;
  OnlineEndpointConfig endpoint_opts;

  const BaseFloat chunk_length_secs = 0.5;
  bool do_endpointing = true;
  constexpr size_t MAX_AUDIO_SIZE = 30 * 32000; // 60 second audio data

  po.Register("word-symbol-table", &word_syms_rxfilename,
              "Symbol table for words [for debug output]");
  po.Register("do-endpointing", &do_endpointing,
              "If true, apply endpoint detection");

  feature_opts.Register(&po);
  decodable_opts.Register(&po);
  decoder_opts.Register(&po);
  endpoint_opts.Register(&po);

  po.Read(argc, argv);

  if (po.NumArgs() != 2) {
    std::cout << "po.NumArgs()" << po.NumArgs() << std::endl;
    po.PrintUsage();
    return 1;
  }

  std::string nnet3_rxfilename = po.GetArg(1), fst_rxfilename = po.GetArg(2);

  OnlineNnet2FeaturePipelineInfo feature_info(feature_opts);

  Matrix<double> global_cmvn_stats;
  if (feature_opts.global_cmvn_stats_rxfilename != "")
    ReadKaldiObject(feature_opts.global_cmvn_stats_rxfilename,
                    &global_cmvn_stats);

  TransitionModel trans_model;
  nnet3::AmNnetSimple am_nnet;
  {
    bool binary;
    Input ki(nnet3_rxfilename, &binary);
    trans_model.Read(ki.Stream(), binary);
    am_nnet.Read(ki.Stream(), binary);
    SetBatchnormTestMode(true, &(am_nnet.GetNnet()));
    SetDropoutTestMode(true, &(am_nnet.GetNnet()));
    nnet3::CollapseModel(nnet3::CollapseModelConfig(), &(am_nnet.GetNnet()));
  }

  // this object contains precomputed stuff that is used by all decodable
  // objects.  It takes a pointer to am_nnet because if it has iVectors it has
  // to modify the nnet to accept iVectors at intervals.
  nnet3::DecodableNnetSimpleLoopedInfo decodable_info(decodable_opts,
                                                      &am_nnet);

  fst::Fst<fst::StdArc> *decode_fst = ReadFstKaldiGeneric(fst_rxfilename);

  fst::SymbolTable *word_syms = NULL;
  if (word_syms_rxfilename != "")
    if (!(word_syms = fst::SymbolTable::ReadText(word_syms_rxfilename)))
      KALDI_ERR << "Could not read symbol table from file "
                << word_syms_rxfilename;

  auto *adaptation_state =new OnlineIvectorExtractorAdaptationState(
                                          feature_info.ivector_extractor_info);

  auto *feature_pipeline = new OnlineNnet2FeaturePipeline(feature_info);
  OnlineCmvnState cmvn_state(global_cmvn_stats);
  feature_pipeline->SetAdaptationState(*adaptation_state);
  feature_pipeline->SetCmvnState(cmvn_state);

  auto *silence_weighting = new OnlineSilenceWeighting(trans_model,
                                          feature_info.silence_weighting_config,
                                          decodable_opts.frame_subsampling_factor);

  auto *decoder = new SingleUtteranceNnet3Decoder(decoder_opts, trans_model,
                                      decodable_info,
                                      *decode_fst, feature_pipeline);

  kaldi::OnlineAudioSource au_src(chunk_length_secs);
  std::thread capture_thread(&kaldi::OnlineAudioSource::Produce, &au_src);  // start portaudio capture thread

  size_t total_audio_size = 0;

  while (1) {
    bool endpoint = false;

    auto audio_chunck = au_src.Consume();
    if (audio_chunck.Dim() <= 0) continue;
    total_audio_size += audio_chunck.Dim();
    feature_pipeline->AcceptWaveform(au_src.SampleRate(), audio_chunck);

    std::vector<std::pair<int32, BaseFloat> > delta_weights;
    if (silence_weighting->Active() &&
        feature_pipeline->IvectorFeature() != NULL) {
      silence_weighting->ComputeCurrentTraceback(decoder->Decoder());
      silence_weighting->GetDeltaWeights(feature_pipeline->NumFramesReady(),
                                        &delta_weights);
      feature_pipeline->IvectorFeature()->UpdateFrameWeights(delta_weights);
    }

    decoder->AdvanceDecoding();
    if ((do_endpointing && decoder->EndpointDetected(endpoint_opts))
          || total_audio_size >= MAX_AUDIO_SIZE) {
      feature_pipeline->InputFinished();
      decoder->AdvanceDecoding();
      decoder->FinalizeDecoding();
      endpoint = true;
    }
    if (decoder->NumFramesDecoded() == 0) continue;

    CompactLattice clat;
    decoder->GetLattice(endpoint, &clat);

    std::string best_result = GetDiagnosticsAndPrintOutput(word_syms, clat);
    if (!best_result.empty()) {
      if (endpoint) {
        std::cout << best_result << "." << std::endl;
      } else {
        std::cout << best_result << std::endl;
      }
    }

    if (endpoint) {
      total_audio_size = 0;

      // free decoder resources and create new
      delete adaptation_state;
      adaptation_state = new OnlineIvectorExtractorAdaptationState(
                                          feature_info.ivector_extractor_info);
      delete feature_pipeline;
      feature_pipeline = new OnlineNnet2FeaturePipeline(feature_info);
      feature_pipeline->SetAdaptationState(*adaptation_state);
      OnlineCmvnState cmvn_state(global_cmvn_stats);
      feature_pipeline->SetCmvnState(cmvn_state);

      delete silence_weighting;
      silence_weighting = new OnlineSilenceWeighting(trans_model,
                                          feature_info.silence_weighting_config,
                                          decodable_opts.frame_subsampling_factor);
      delete decoder;
      decoder = new SingleUtteranceNnet3Decoder(decoder_opts, trans_model,
                                                decodable_info,
                                                *decode_fst, feature_pipeline);
    }
  }

  capture_thread.join();

  // free resources
  delete adaptation_state;
  delete feature_pipeline;
  delete silence_weighting;
  delete decoder;

  return 0;
} // main()
