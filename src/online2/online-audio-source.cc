// online2/online-audio-source.cc

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

namespace kaldi {

void OnlineAudioSource::Init() {
  /* -- initialize PortAudio -- */
  err_ = Pa_Initialize();
  if(err_ != paNoError) goto error;

  /* -- setup input and output -- */
  inputParameters_.device = Pa_GetDefaultInputDevice();  /* default input device */
  if (inputParameters_.device == -1) {
    err_ = paNotInitialized;
    KALDI_WARN << "Open default input device error";
    goto error;
  }

  inputParameters_.channelCount = NUM_CHANNELS;
  inputParameters_.sampleFormat = PA_SAMPLE_TYPE;

  inputParameters_.suggestedLatency = Pa_GetDeviceInfo(inputParameters_.device)->defaultHighInputLatency;

  inputParameters_.hostApiSpecificStreamInfo = NULL;

  outputParameters_.device = Pa_GetDefaultOutputDevice();  /* default output device */
  if (outputParameters_.device == -1) {
    err_ = paNotInitialized;
    KALDI_WARN << "Open default output device error";
    goto error;
  }
  outputParameters_.channelCount = NUM_CHANNELS;
  outputParameters_.sampleFormat = PA_SAMPLE_TYPE;
  outputParameters_.suggestedLatency =
        Pa_GetDeviceInfo(outputParameters_.device)->defaultHighOutputLatency;
  outputParameters_.hostApiSpecificStreamInfo = NULL;

  /* -- setup stream -- */
  err_ = Pa_OpenStream(&stream_, &inputParameters_, &outputParameters_,
                        SAMPLE_RATE, FRAMES_PER_BUFFER, paClipOff,  /* we won't output out of range samples so don't bother clipping them */
                        NULL,  /* no callback, use blocking API */
                        NULL );  /* no callback, so no callback userData */
  if(err_ != paNoError) {
    KALDI_WARN << "Pa open stream error";
    goto error;
  } 

  /* -- start stream -- */
  err_ = Pa_StartStream(stream_);
  if(err_ != paNoError) {
    KALDI_WARN << "Pa start stream error";
    goto error;
  }
  
  return;

  error:
      Pa_Terminate();
      running_ = false;
}

void OnlineAudioSource::Produce() {
  if (err_ != paNoError) return;
  while (running_) {
    err_ = Pa_ReadStream(stream_, sampleBlock_.data(), chunck_size_ / sizeof(short));  /* block until block filled */
    if (err_ != paNoError) KALDI_WARN << "Stream error";

    std::unique_lock<std::mutex> mlock(mutex_);
    queue_.push(sampleBlock_);
    mlock.unlock();
    cond_.notify_one();
  }
}

kaldi::Vector<BaseFloat> OnlineAudioSource::Consume() {
  std::unique_lock<std::mutex> mlock(mutex_);
  while (queue_.empty()) {
    cond_.wait(mlock);
  }
  auto byte_data = queue_.front();
  queue_.pop();

  char *buffer_p = const_cast<char *>(byte_data.data());
  uint16 *data_ptr = reinterpret_cast<uint16 *>(buffer_p);

  kaldi::Vector<BaseFloat> data(byte_data.size() / 2);
  for (uint32 i = 0; i < data.Dim(); ++i) {
    int16 k = *data_ptr++;
    data(i) = static_cast<BaseFloat> (k);
  }

  return data;
}

} // namespace kaldi
