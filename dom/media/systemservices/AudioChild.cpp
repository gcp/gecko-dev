/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "prlog.h"

#include "AudioChild.h"
#include "CamerasUtils.h"

PRLogModuleInfo *gAudioChildLog;

#undef LOG
#undef LOG_ENABLED
#if defined(PR_LOGGING)
#define LOG(args) PR_LOG(gAudioChildLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gAudioChildLog, 5)
#else
#define LOG(args)
#define LOG_ENABLED() (false)
#endif

namespace mozilla {
namespace audio {

static PAudioChild* sAudio;
nsCOMPtr<nsIThread> sAudioChildThread;

static AudioChild*
Audio()
{
  if (!sAudio) {
    // Try to get the PBackground handle
    ipc::PBackgroundChild* existingBackgroundChild =
      ipc::BackgroundChild::GetForCurrentThread();
    // If it's not spun up yet, block until it is, and retry
    if (!existingBackgroundChild) {
      camera::SynchronouslyCreatePBackground();
      existingBackgroundChild =
        ipc::BackgroundChild::GetForCurrentThread();
    }
    // By now PBackground is guaranteed to be up
    MOZ_RELEASE_ASSERT(existingBackgroundChild);
    // Create PCameras by sending a message to the parent
    sAudio = existingBackgroundChild->SendPAudioConstructor();
    sAudioChildThread = NS_GetCurrentThread();
  }
  MOZ_ASSERT(sAudio);
  return static_cast<AudioChild*>(sAudio);
}

int GetMaxChannelCount()
{
  LOG((__PRETTY_FUNCTION__));
  int channels = 0;
  if (Audio()->SendGetMaxChannelCount(&channels)) {
    return channels;
  } else {
    return 0;
  }
}

int GetMinLatency(AudioStreamParams params)
{
  LOG((__PRETTY_FUNCTION__));
  int minlatency = 0;
  if (Audio()->SendGetMinLatency(params, &minlatency)) {
    return minlatency;
  } else {
    return 0;
  }
}

int GetPreferredSampleRate()
{
  LOG((__PRETTY_FUNCTION__));
  int rate = 0;
  if (Audio()->SendGetPreferredSampleRate(&rate)) {
    return rate;
  } else {
    return 0;
  }
}

AudioChild::AudioChild()
{
#if defined(PR_LOGGING)
  if (!gAudioChildLog)
    gAudioChildLog = PR_NewLogModule("AudioChild");
#endif

  LOG(("AudioChild: %p", this));

  MOZ_COUNT_CTOR(AudioChild);
}

AudioChild::~AudioChild()
{
  LOG(("~AudioChild: %p", this));

  MOZ_COUNT_DTOR(AudioChild);
}

PAudioChild* CreateAudioChild() {
  return new AudioChild();
}

}
}
