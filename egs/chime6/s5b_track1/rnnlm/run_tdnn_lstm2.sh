#!/usr/bin/env bash

# Copyright 2012  Johns Hopkins University (author: Daniel Povey)
#           2015  Guoguo Chen
#           2017  Hainan Xu
#           2017  Xiaohui Zhang
#           2020  Ke Li

# This script trains LSTM based LMs on transcription and perform lattice rescoring
# on 1st pass decoding results. The score submission script in stage 5 is for Track 2.
# Note: local/score_for_submit_track2.sh is copied from ../s5_track2/local/score_for_submit.sh
# Begin configuration section.

dir=exp/rnnlm_lstm_1b
embedding_dim=512
lstm_rpd=128
lstm_nrpd=128
embedding_l2=0.003 # embedding layer l2 regularize
comp_l2=0.003 # component-level l2 regularize
output_l2=0.001 # output-layer l2 regularize
stage=-10
train_stage=-10
score_stage=0

# variables for lattice rescoring
run_lat_rescore=true
run_nbest_rescore=true
run_backward_rnnlm=false

#Track 2 Ashish' model that gives dev 78.24 and eval 73.55 in the google sheet (ASR)
# dir = ${ac_model_dir}/{dev,eval}_beamformit_dereverb_diarized_U06_2stage

#Track 2 same acoustic model (I assume), gss preprocess;
ac_model_dir=exp/chain_train_worn_simu_u400k_cleaned_rvb/tdnn1b_cnn_l2_03_4500_ep6_sp_track2
# ac_model_dir=/export/c11/asubraman/kaldi_chime6/challenge/kaldi_chime6_jhu/egs/chime6/s5_track2/exp/combine/
# dev_lattice_combination_gss-MA_U06_U04_overlap_best_dev_0.0

decode_dir_suffix=rnnlm_1b
# enhancement=beamformit
# Track2 dev/eval WERs are 69.28/68.78 decode_{dev, eval}_gss_MA_cs320000_rttm_overlap_dereverb_diarized_2stage
# With rescoring, I get 68.69/67.94
# enhancement=gss_MA_cs320000_rttm_overlap
# Track 2 dev/eval WERs are 70.97/68.75, this corresponds to line34 in GSS-track2 of the google sheet
# enhancement=gss_MA_cs320000_rttm
# system 12
# enhancement=gss_cs480000_it5_rttm_overlap_min200ms_U04
enhancement=lattice_combination_gss-MA_U06_U04_overlap_best_dev_0.0

chime6_corpus=${PWD}/CHiME6
json_dir=${chime6_corpus}/transcriptions
ngram_order=4 # approximate the lattice-rescoring by limiting the max-ngram-order
              # if it's set, it merges histories in the lattice if they share
              # the same ngram history and this prevents the lattice from 
              # exploding exponentially
pruned_rescore=true

. ./cmd.sh
. ./utils/parse_options.sh

train_text=data/train_worn/text
dev_text=data/dev_worn/text
text_dir=data/rnnlm/text
mkdir -p $dir/config
set -e

for f in $text; do
  [ ! -f $f ] && \
    echo "$0: expected file $f to exist; search for local/wsj_extend_dict.sh in run.sh" && exit 1
done

if [ $stage -le 0 ]; then
  mkdir -p $text_dir
  cat $train_text | cut -d ' ' -f2- > $text_dir/train.txt
  cat $dev_text | cut -d ' ' -f2- > $text_dir/dev.txt
fi

if [ $stage -le 1 ]; then
  cp data/lang/words.txt $dir/config/
  n=`cat $dir/config/words.txt | wc -l`
  echo "<brk> $n" >> $dir/config/words.txt

  # words that are not present in words.txt but are in the training or dev data, will be
  # mapped to <unk> during training.
  echo "<unk>" >$dir/config/oov.txt

  cat > $dir/config/data_weights.txt <<EOF
train   1   1.0
EOF

  rnnlm/get_unigram_probs.py --vocab-file=$dir/config/words.txt \
                             --unk-word="<unk>" \
                             --data-weights-file=$dir/config/data_weights.txt \
                             $text_dir | awk 'NF==2' >$dir/config/unigram_probs.txt

  # choose features
  rnnlm/choose_features.py --unigram-probs=$dir/config/unigram_probs.txt \
                           --use-constant-feature=true \
                           --special-words='<s>,</s>,<brk>,<unk>,[inaudible],[noise],[laughs]' \
                           $dir/config/words.txt > $dir/config/features.txt

lstm_opts="l2-regularize=$comp_l2"
tdnn_opts="l2-regularize=$comp_l2"
output_opts="l2-regularize=$output_l2"

  cat >$dir/config/xconfig <<EOF
input dim=$embedding_dim name=input
relu-renorm-layer name=tdnn1 dim=$embedding_dim $tdnn_opts input=Append(0, IfDefined(-1))
fast-lstmp-layer name=lstm1 cell-dim=$embedding_dim recurrent-projection-dim=$lstm_rpd non-recurrent-projection-dim=$lstm_nrpd $lstm_opts
relu-renorm-layer name=tdnn2 dim=$embedding_dim $tdnn_opts input=Append(0, IfDefined(-3))
fast-lstmp-layer name=lstm2 cell-dim=$embedding_dim recurrent-projection-dim=$lstm_rpd non-recurrent-projection-dim=$lstm_nrpd $lstm_opts
relu-renorm-layer name=tdnn3 dim=$embedding_dim $tdnn_opts input=Append(0, IfDefined(-3))
output-layer name=output $output_opts include-log-softmax=false dim=$embedding_dim
EOF
  rnnlm/validate_config_dir.sh $text_dir $dir/config
fi

if [ $stage -le 2 ]; then
  rnnlm/prepare_rnnlm_dir.sh $text_dir $dir/config $dir
fi

if [ $stage -le 3 ]; then
  rnnlm/train_rnnlm.sh --num-jobs-initial 1 --num-jobs-final 1 \
                       --embedding_l2 $embedding_l2 \
                       --stage $train_stage --num-epochs 60 --cmd "$train_cmd" $dir
fi

# old 3-gram LM is data/lang/G.fst 
if [ $stage -le 4 ] && $run_lat_rescore; then
  echo "$0: Perform lattice-rescoring on $ac_model_dir"
  pruned=
  if $pruned_rescore; then
    pruned=_pruned
  fi
  # for decode_set in dev_${enhancement}_dereverb_diarized eval_${enhancement}_dereverb_diarized; do
  for decode_set in dev_${enhancement} eval_${enhancement}; do
    # decode_dir=${ac_model_dir}/decode_${decode_set}_2stage
    decode_dir=${ac_model_dir}/decode_${decode_set}

    # Lattice rescoring
    rnnlm/lmrescore$pruned.sh \
      --cmd "$decode_cmd --mem 4G" \
      --acwt 0.1 \
      --weight 0.4 --max-ngram-order $ngram_order \
      data/lang $dir \
      data/${decode_set}_hires ${decode_dir} \
      ${decode_dir}_${decode_dir_suffix}_0.4
  done
fi

if [ $stage -le 5 ]; then
  # final scoring to get the official challenge result
  # please specify both dev and eval set directories so that the search parameters
  # (insertion penalty and language model weight) will be tuned using the dev set
      #--dev_datadir dev_${enhancement}_dereverb_diarized_U06_hires \
      #--eval_datadir eval_${enhancement}_dereverb_diarized_U06_hires
  local/score_for_submit_track2.sh --stage $score_stage \
      --dev_decodedir ${ac_model_dir}/decode_dev_${enhancement}_dereverb_diarized_2stage_${decode_dir_suffix}_0.4 \
      --dev_datadir dev_${enhancement}_dereverb_diarized_hires \
      --eval_decodedir ${ac_model_dir}/decode_eval_${enhancement}_dereverb_diarized_2stage_${decode_dir_suffix}_0.4 \
      --eval_datadir eval_${enhancement}_dereverb_diarized_hires
fi
enhancement=gss_MA_cs320000_rttm_overlap
#enhancement=gss_cs480000_it5_rttm_overlap_min200ms
# old 3-gram LM is data/lang/G.fst 
if [ $stage -le 6 ] && $run_lat_rescore; then
  echo "$0: Perform lattice-rescoring on $ac_model_dir"
  pruned=
  if $pruned_rescore; then
    pruned=_pruned
  fi
  for decode_set in dev_${enhancement}_dereverb_diarized eval_${enhancement}_dereverb_diarized; do
    decode_dir=${ac_model_dir}/decode_${decode_set}_2stage

    # Lattice rescoring
    rnnlm/lmrescore$pruned.sh \
      --cmd "$decode_cmd --mem 4G" \
      --acwt 0.1 \
      --weight 0.4 --max-ngram-order $ngram_order \
      data/lang $dir \
      data/${decode_set}_hires ${decode_dir} \
      ${decode_dir}_${decode_dir_suffix}_0.4
  done
fi

if [ $stage -le 7 ]; then
  # final scoring to get the official challenge result
  # please specify both dev and eval set directories so that the search parameters
  # (insertion penalty and language model weight) will be tuned using the dev set
  local/score_for_submit_track2.sh --stage $score_stage \
      --dev_decodedir ${ac_model_dir}/decode_dev_${enhancement}_dereverb_diarized_2stage_${decode_dir_suffix}_0.4 \
      --dev_datadir dev_${enhancement}_dereverb_diarized_hires \
      --eval_decodedir ${ac_model_dir}/decode_eval_${enhancement}_dereverb_diarized_2stage_${decode_dir_suffix}_0.4 \
      --eval_datadir eval_${enhancement}_dereverb_diarized_hires
fi
exit 0
