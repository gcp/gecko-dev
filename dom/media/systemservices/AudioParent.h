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

#include "AudioChild.h"

namespace mozilla {
namespace audio {

class AudioParent;

class AudioParent :  public PAudioParent
{
public:
  virtual bool RecvGetMaxChannelCount(int *) MOZ_OVERRIDE;
  virtual bool RecvGetMinLatency(const AudioStreamParams&, int*) MOZ_OVERRIDE;
  virtual bool RecvGetPreferredSampleRate(int *) MOZ_OVERRIDE;

  virtual void ActorDestroy(ActorDestroyReason aWhy) MOZ_OVERRIDE;

  nsCOMPtr<nsIThread> GetBackgroundThread() { return mPBackgroundThread; };

  AudioParent();
  virtual ~AudioParent();

protected:
  bool EnsureInitialized();

  // audio buffers1
  bool mShmemInitialized;
  mozilla::ipc::Shmem mShmem;

  // cubeb stuffs
  cubeb* mCubebContext;
  double mVolumeScale;
  uint32_t mCubebLatency;

  // PBackground parent thread
  nsCOMPtr<nsIThread> mPBackgroundThread;
};

PAudioParent* CreateAudioParent();

} // namespace audio
} // namespace mozilla

#endif  // mozilla_AudioParent_h
