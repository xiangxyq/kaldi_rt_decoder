
all:

EXTRA_CXXFLAGS += -Wno-sign-compare -I ../../tools/portaudio/install/include
EXTRA_LDLIBS =

include ../kaldi.mk

UNAME=$(shell uname)
ifeq ($(UNAME), Darwin)
    EXTRA_LDLIBS = ../../tools/portaudio/install/lib/libportaudio.a
    EXTRA_LDLIBS += -framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework CoreServices
endif

ifeq ($(UNAME), Linux)
  ifneq "$(wildcard ../../tools/portaudio/install/lib/libportaudio.a)" ""
    EXTRA_LDLIBS = ../../tools/portaudio/install/lib/libportaudio.a
  else
    EXTRA_LDLIBS = ../../tools/portaudio/install/lib64/libportaudio.a
  endif
  ifneq ($(wildcard ../../tools/portaudio/install/include/pa_linux_alsa.h),)
    EXTRA_LDLIBS += -lasound -lrt
  else
    EXTRA_LDLIBS += -lrt
  endif
  ifneq ($(wildcard ../../tools/portaudio/install/include/pa_jack.h),)
    EXTRA_LDLIBS += -ljack
  endif
endif

TESTFILES =

OBJFILES = online-gmm-decodable.o online-feature-pipeline.o online-ivector-feature.o \
           online-nnet2-feature-pipeline.o online-gmm-decoding.o online-timing.o \
           online-endpoint.o onlinebin-util.o online-speex-wrapper.o \
           online-nnet2-decoding.o online-nnet2-decoding-threaded.o \
           online-nnet3-decoding.o online-nnet3-incremental-decoding.o \
           online-nnet3-wake-word-faster-decoder.o online-audio-source.o

LIBNAME = kaldi-online2

ADDLIBS = ../ivector/kaldi-ivector.a ../nnet3/kaldi-nnet3.a \
          ../chain/kaldi-chain.a ../nnet2/kaldi-nnet2.a \
          ../cudamatrix/kaldi-cudamatrix.a ../decoder/kaldi-decoder.a \
          ../lat/kaldi-lat.a ../hmm/kaldi-hmm.a ../feat/kaldi-feat.a \
          ../transform/kaldi-transform.a ../gmm/kaldi-gmm.a \
          ../tree/kaldi-tree.a ../util/kaldi-util.a ../matrix/kaldi-matrix.a \
          ../base/kaldi-base.a 

include ../makefiles/default_rules.mk

