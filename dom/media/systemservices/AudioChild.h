/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AudioChild_h
#define mozilla_AudioChild_h

#include "mozilla/audio/PAudioChild.h"
#include "mozilla/audio/PAudioParent.h"
#include "cubeb/cubeb.h"

namespace mozilla {
namespace audio {

int GetMaxChannelCount();
int GetMinLatency(AudioStreamParams params);
int GetPreferredSampleRate();
int StreamInit(const nsCString& name,
               int * stream_id,
               AudioStreamParams params,
               unsigned int latency,
               cubeb_data_callback data_callback,
               cubeb_state_callback state_callback,
               void * user_ptr);

class StreamHelper
{
public:
  int stream_id;
  cubeb_data_callback data_callback;
  cubeb_state_callback state_callback;
  void * user_ptr;
};

class AudioChild :
  public PAudioChild
{
public:
  AudioChild();
  virtual ~AudioChild();
  int StreamInit(const nsCString& name,
                 int * stream_id,
                 AudioStreamParams params,
                 unsigned int latency,
                 cubeb_data_callback data_callback,
                 cubeb_state_callback state_callback,
                 void * user_ptr);

private:
  nsTArray<StreamHelper*> mChildStreams;
};

PAudioChild* CreateAudioChild();

} // namespace audio
} // namespace mozilla

#endif  // mozilla_AudioChild_h
