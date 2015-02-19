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

cubeb_stream_params
AudioParent::ToCubebParams(AudioStreamParams & aParams)
{
  cubeb_stream_params params;
  params.format = static_cast<cubeb_sample_format>(aParams.format());
  params.rate = aParams.rate();
  params.channels = aParams.channels();
#ifdef ANDROID
  params.stream_type = static_cast<cubeb_stream_type>(aParams.stream_type());
#endif
  return params;
}

bool
AudioParent::RecvGetMinLatency(const AudioStreamParams& aParams, int *aMinLatency)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized()) {
    return false;
  }
  uint32_t latency;
  cubeb_stream_params params = ToCubebParams(aParams);
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

class DataCallbackRunnable : public nsRunnable {
public:
  DataCallbackRunnable(int aStreamId, mozilla::ipc::Shmem&& aShmem, int aFrames)
    : mStreamId(aStreamId), mShmem(aShmem), mFrames(aFrames) {};

  NS_IMETHOD Run() {
    sAudioParent->SendDataCallback(mStreamId, mShmem, mFrames);
    return NS_OK;
  }

private:
  int mStreamId;
  mozilla::ipc::Shmem&& aShmem;
  void *mBuffer;
  int mFrames;
};

long
AudioParent::DataCallback(cubeb_stream *aStream, void* aBuffer, long aFrames)
{
  MonitorAutoLock lock(mMonitor);
  int stream_id = -1;
  for (int i = 0; i < mStreams.Length(); i++) {
    if (mStreams[i].stream == aStream) {
      stream_id = i;
      break;
    }
  }
  if (stream_id == -1) {
    return CUBEB_ERROR;
  }

  mStreams[i].buffer = aBuffer;
  mStreams[i].filled_frames = 0;

  nsRefPtr<DataCallbackRunnable> runnable =
    new DataCallbackRunnable(stream_id, mShmem, aFrames);
  sAudioParent->GetBackgroundThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);

  // wait for signal that we received data back, effectively
  // blocking us
  do {
    mMonitor.Wait();
  } while (mStreams[i].filled_frames != 0);

  return mStreams[i].filled_frames;
}

bool
AudioParent::RecvDataDelivery(const int& stream_id,
                              mozilla::ipc::Shmem&& buffer,
                              const int& delivered_frames)
{
  MonitorAutoLock lock(mMonitor);
  StreamInfo * stream_info = nullptr;
  if (stream_id < mStreams.Length()) {
    stream_info = &mStreams[stream_id];
  } else {
    return false;
  }

  if (stream_info->buffer == nullptr) {
    return false;
  }

  size_t len = delivered_frames * stream_info->channels
               * CubebFormatToBytes(stream_info->format);
  memcpy(mShmem.get<char>(), stream_info->buffer, len);
  streaminfo->filled_frames = delivered_frames;

  // signal
  mMonitor.NotifyAll();
}

void
AudioParent::StateCallback(cubeb_stream *aStream, cubeb_state aState)
{
  // XXX
}

bool
AudioParent::RecvStreamInit(const nsCString& aName,
                            const AudioStreamParams& aParams,
                            const int& aLatency,
                            int* aId)
{
  MonitorAutoLock lock(mMonitor);
  cubeb_stream *stream;
  cubeb_stream_params params = ToCubebParams(aParams);
  int rv = cubeb_stream_init(mCubebContext,
                             &stream,
                             aName.get(),
                             params,
                             latency,
                             DataCallback_S,
                             StateCallback_S,
                             this);
  if (rv != CUBEB_OK) {
    return false;
  }
  *aId = mStreams.Length();
  StreamInfo streaminfo;
  streaminfo.stream = stream;
  streaminfo.channels = aParams.channels;
  streaminfo.format = aParams.format;
  streaminfo.buffer = nullptr;
  streaminfo.filled_frames = 0;
  mStreams.AppendElement(streaminfo);
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
  : //mShmemInitialized(false),
    mCubebContext(nullptr),
    mVolumeScale(1.0),
    mCubebLatency(0),
    mMonitor("AudioParent")
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

  // XXX who needs to do this
  DeallocShmem(mShmem);

  for (int i = 0; i < mStreams.Length(); i++) {
    cubeb_stream_destroy(mStreams[i].stream);
  }

  cubeb_destroy(mCubebContext);
}

PAudioParent* CreateAudioParent() {
  return new AudioParent();
}

}
}
