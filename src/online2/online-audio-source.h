// online2/online-audio-source.h

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

#ifndef KALDI_ONLINE2_ONLINE_AUDIO_SOURCE_H_
#define KALDI_ONLINE2_ONLINE_AUDIO_SOURCE_H_

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <portaudio.h>
#include <pa_ringbuffer.h>

#include "base/kaldi-error.h"
#include "matrix/kaldi-vector.h"


namespace kaldi {

class OnlineAudioSource {
  public:
    OnlineAudioSource(BaseFloat chunck = 0.5):
        chunck_size_(static_cast<size_t> (chunck * 32000)) {  /* time chunck, unit:seconds, default chunck is 0.2 seconds*/
      Init();
      sampleBlock_.resize(chunck_size_);
    }

    void Produce();

    kaldi::Vector<BaseFloat> Consume();

    void Start() { running_ = true; }

    void Stop() { running_ = false; }

    double SampleRate() const { return SAMPLE_RATE; }

  private:
    void Init();

    const int NUM_CHANNELS = 1;
    const PaSampleFormat PA_SAMPLE_TYPE = paInt16;
    const double SAMPLE_RATE = 16000.0;
    const unsigned long FRAMES_PER_BUFFER = 1024;

    PaStream *stream_;
    PaStreamParameters inputParameters_;
    PaStreamParameters outputParameters_;
    PaError err_;

    std::vector<char> sampleBlock_;

    std::mutex mutex_;
    std::condition_variable cond_;
    std::queue<std::vector<char> > queue_;

    bool running_ = true;
    const size_t chunck_size_;
};

} // namespace kaldi

#endif // KALDI_ONLINE2_ONLINE_AUDIO_SOURCE_H_
