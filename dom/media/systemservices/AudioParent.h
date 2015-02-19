/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AudioParent_h
#define mozilla_AudioParent_h

#include "mozilla/audio/PAudioParent.h"
#include "mozilla/ipc/Shmem.h"
#include "cubeb/cubeb.h"
#include "mozilla/Monitor.h"

#include "AudioChild.h"

namespace mozilla {
namespace audio {

struct StreamInfo
{
  cubeb_stream *stream;
  int channels;
  int format;
  int filled_frames;
  void *buffer;
};

class AudioParent :  public PAudioParent
{
public:
  virtual bool RecvGetMaxChannelCount(int *) MOZ_OVERRIDE;
  virtual bool RecvGetMinLatency(const AudioStreamParams&, int*) MOZ_OVERRIDE;
  virtual bool RecvGetPreferredSampleRate(int *) MOZ_OVERRIDE;
  virtual bool RecvStreamInit(const nsCString& name, const AudioStreamParams& params,
                              const int& latency, int* id) MOZ_OVERRIDE;
  virtual bool RecvDataDelivery(const int& stream_id,
                                mozilla::ipc::Shmem&& buffer,
                                const int& deliver_frames) MOZ_OVERRIDE;

  virtual void ActorDestroy(ActorDestroyReason aWhy) MOZ_OVERRIDE;

  nsCOMPtr<nsIThread> GetBackgroundThread() { return mPBackgroundThread; };

  AudioParent();
  virtual ~AudioParent();

private:
  bool EnsureInitialized();
  static cubeb_stream_params ToCubebParams(AudioStreamParams & params);
  long DataCallback(cubeb_stream *aStream, void* aBuffer, long aFrames);
  void StateCallback(cubeb_stream *aStream, cubeb_state aState);

  // cubeb callbacks
  static long DataCallback_S(cubeb_stream* aStream, void* aThis, void* aBuffer, long aFrames) {
    return static_cast<AudioParent*>(aThis)->DataCallback(aStream, aBuffer, aFrames);
  }
  static void StateCallback_S(cubeb_stream* aStream, void* aThis, cubeb_state aState) {
    static_cast<AudioParent*>(aThis)->StateCallback(aStream, aState);
  }

  // audio buffers
  // bool mShmemInitialized;
  mozilla::ipc::Shmem mShmem;

  // cubeb stuffs
  cubeb* mCubebContext;
  double mVolumeScale;
  uint32_t mCubebLatency;

  // protects both data and streams
  mozilla::Monitor mMonitor;

  // stream info
  nsTArray<StreamInfo> mStreams;

  // PBackground parent thread
  nsCOMPtr<nsIThread> mPBackgroundThread;
};

PAudioParent* CreateAudioParent();

} // namespace audio
} // namespace mozilla

#endif  // mozilla_AudioParent_h
