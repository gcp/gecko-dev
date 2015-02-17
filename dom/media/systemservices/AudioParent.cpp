/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "AudioParent.h"
#include "nsThreadUtils.h"
#include "prlog.h"

PRLogModuleInfo *gAudioParentLog;

#undef LOG
#undef LOG_ENABLED
#if defined(PR_LOGGING)
#define LOG(args) PR_LOG(gAudioParentLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gAudioParentLog, 5)
#else
#define LOG(args)
#define LOG_ENABLED() (false)
#endif

namespace mozilla {
namespace audio {

static AudioParent* sAudioParent = nullptr;

bool
AudioParent::RecvGetMaxChannelCount(int *aChannelCount)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized()) {
    return false;
  }
  uint32_t channels;
  int rv = cubeb_get_max_channel_count(mCubebContext,
                                       &channels);
  if (rv != CUBEB_OK) {
    return false;
  }
  *aChannelCount = channels;
  return true;
}

bool
AudioParent::RecvGetMinLatency(const AudioStreamParams& aParams, int *aMinLatency)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized()) {
    return false;
  }
  uint32_t latency;
  cubeb_stream_params params;
  params.format = static_cast<cubeb_sample_format>(aParams.format());
  params.rate = aParams.rate();
  params.channels = aParams.channels();
#ifdef ANDROID
  params.stream_type = static_cast<cubeb_stream_type>(aParams.stream_type());
#endif
  int rv = cubeb_get_min_latency(mCubebContext,
                                 params,
                                 &latency);
  if (rv != CUBEB_OK) {
    return false;
  }
  *aMinLatency = latency;
  return true;
}

bool
AudioParent::RecvGetPreferredSampleRate(int *aPreferredRate)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized()) {
    return false;
  }
  uint32_t rate;
  int rv = cubeb_get_preferred_sample_rate(mCubebContext, &rate);
  if (rv != CUBEB_OK) {
    return false;
  }
  *aPreferredRate = rate;
  return true;
}

bool AudioParent::EnsureInitialized()
{
  if (mCubebContext != nullptr) {
    return true;
  }

  int rv = cubeb_init(&mCubebContext, "AudioParent");
  if (rv != CUBEB_OK) {
    MOZ_ASSERT(mCubebContext == nullptr);
    return false;
  }

  MOZ_ASSERT(mCubebContext != nullptr);

  return true;
}

void
AudioParent::ActorDestroy(ActorDestroyReason aWhy)
{
  // No more IPC from here
  LOG((__PRETTY_FUNCTION__));
}

AudioParent::AudioParent()
  : mShmemInitialized(false),
    mCubebContext(nullptr),
    mVolumeScale(1.0),
    mCubebLatency(0)
{
#if defined(PR_LOGGING)
  if (!gAudioParentLog)
    gAudioParentLog = PR_NewLogModule("AudioParent");
#endif
  LOG(("AudioParent: %p", this));

  mPBackgroundThread = NS_GetCurrentThread();
  sAudioParent = this;

  MOZ_COUNT_CTOR(AudioParent);
}

AudioParent::~AudioParent()
{
  LOG(("~AudioParent: %p", this));

  MOZ_COUNT_DTOR(AudioParent);

  DeallocShmem(mShmem);

  for (int i = 0; i < mStreams.Length(); i++) {
    cubeb_stream_destroy(mStreams[i]);
  }

  cubeb_destroy(mCubebContext);
}

PAudioParent* CreateAudioParent() {
  return new AudioParent();
}

}
}
