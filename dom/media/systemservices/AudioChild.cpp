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
  MOZ_ASSERT(sAudioChildThread == NS_GetCurrentThread());
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
  MOZ_ASSERT(sAudioChildThread == NS_GetCurrentThread());
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
  MOZ_ASSERT(sAudioChildThread == NS_GetCurrentThread());
  int rate = 0;
  if (Audio()->SendGetPreferredSampleRate(&rate)) {
    return rate;
  } else {
    return 0;
  }
}

int StreamInit(const nsCString& name,
               int * stream_id,
               AudioStreamParams params,
               unsigned int latency,
               cubeb_data_callback data_callback,
               cubeb_state_callback state_callback,
               void * user_ptr)
{
  LOG((__PRETTY_FUNCTION__));
  MOZ_ASSERT(sAudioChildThread == NS_GetCurrentThread());
  return Audio()->StreamInit(name, stream_id, params, latency,
                             data_callback, state_callback, user_ptr);
}

static int
AudioChild::CubebFormatToBytes(cubeb_sample_format aFormat)
{
  switch (aFormat) {
    case CUBEB_SAMPLE_S16LE:
    case CUBEB_SAMPLE_S16BE:
      return 2;
    case CUBEB_SAMPLE_FLOAT32LE:
    case CUBEB_SAMPLE_FLOAT32BE:
      return 4;
    default:
      MOZ_CRASH("Unknown format");
  }
}

bool
AudioChild::RecvDataCallback(const int& stream_id,
                             mozilla::ipc::Shmem&& aShmem,
                             const int& num_frames)
{
  StreamHelper * helper = nullptr;
  for (int i = 0; i < mChildStream.Length(); i++) {
    if (mChildStream[i].stream_id == stream_id) {
      helper = &mChildStream[i];
      break;
    }
  }

  if (!helper) {
    return false;
  }

  size_t buffer_size = num_frames * helper->channels
                       * CubebFormatToBytes(helper->format);
  mShmem = aShmem;

  if (!mShmemInitialized) {
    AllocShmem(buffer_size, SharedMemory::TYPE_BASIC, &mShmem);
    mShmemInitialized = true;
  }

  if (!mShmem.IsWritable()) {
    LOG(("Audio shmem is not writeable in %s", __PRETTY_FUNCTION__));
    return -1;
  }

  // Prepare audi buffer
  if (mShmem.Size<char>() != buffer_size) {
    DeallocShmem(mShmem);
    // this may fail; always check return value
    if (!AllocShmem(buffer_size, SharedMemory::TYPE_BASIC, &mShmem)) {
      LOG(("Failure allocating new size audio buffer"));
      return -1;
    }
  }

  // call callback
  int frames = helper->data_callback(nullptr, // hell will break loose if anyone starts using this
                                     helper->user_ptr,
                                     mShmem.get<char>(),
                                     num_frames);

  if (frames == CUBEB_ERROR) {
    return false;
  }

  Audio()->SendDataDelivery(stream_id, mShmem, frames);

  return true;
}

int
AudioChild::StreamInit(const nsCString& name,
                       int * stream_id,
                       AudioStreamParams params,
                       unsigned int latency,
                       cubeb_data_callback data_callback,
                       cubeb_state_callback state_callback,
                       void * user_ptr)
{
  StreamHelper * helper = new StreamHelper();

  helper->data_callback = data_callback;
  helper->state_callback = state_callback;
  helper->user_ptr = user_ptr;
  helper->channels = params.channels;
  helper->format = params.format;

  if (!SendStreamInit(name, params, latency, &helper->stream_id)) {
    return CUBEB_ERROR;
  }

  mChildStreams.AppendElement(helper);

  return CUBEB_OK;
}

AudioChild::AudioChild()
  : mShmemInitialized(false)
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

  for (int i = 0; i < mChildStreams.Length(); i++) {
    delete mChildStreams[i];
  }

  MOZ_COUNT_DTOR(AudioChild);
}

PAudioChild* CreateAudioChild() {
  return new AudioChild();
}

}
}
