/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaEngineRemoteVideoSource.h"

#include "mozilla/RefPtr.h"
#include "VideoUtils.h"
#include "nsIPrefService.h"
#include "MediaTrackConstraints.h"

namespace mozilla {

using dom::ConstrainLongRange;

NS_IMPL_ISUPPORTS0(MediaEngineRemoteVideoSource)

MediaEngineRemoteVideoSource::MediaEngineRemoteVideoSource(
  int aIndex, const char* aMonitorName)
  : MediaEngineCameraVideoSource(aIndex, aMonitorName)
{
  Init();
}

void
MediaEngineRemoteVideoSource::Init() {
  return;
}

void
MediaEngineRemoteVideoSource::Shutdown() {
  return;
}

nsresult
MediaEngineRemoteVideoSource::Allocate(const VideoTrackConstraintsN& aConstraints,
                                       const MediaEnginePrefs& aPrefs)
{

  ConstrainLongRange cWidth(aConstraints.mRequired.mWidth);
  ConstrainLongRange cHeight(aConstraints.mRequired.mHeight);

  if (aConstraints.mAdvanced.WasPassed()) {
    const auto& advanced = aConstraints.mAdvanced.Value();
    for (uint32_t i = 0; i < advanced.Length(); i++) {
      if (cWidth.mMax >= advanced[i].mWidth.mMin && cWidth.mMin <= advanced[i].mWidth.mMax &&
         cHeight.mMax >= advanced[i].mHeight.mMin && cHeight.mMin <= advanced[i].mHeight.mMax) {
        cWidth.mMin = std::max(cWidth.mMin, advanced[i].mWidth.mMin);
        cHeight.mMin = std::max(cHeight.mMin, advanced[i].mHeight.mMin);
      }
    }
  }

  // cWidth.mMin cWidth.mMax
  // mTimePerFrame = aPrefs.mFPS ? 1000 / aPrefs.mFPS : aPrefs.mFPS;

  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Deallocate()
{
  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Start(SourceMediaStream* aStream, TrackID aID)
{
  //aStream->AddTrack(aID, 0, new VideoSegment());
  //aStream->AdvanceKnownTracksTime(STREAM_TIME_MAX);
  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Stop(mozilla::SourceMediaStream*, mozilla::TrackID)
{
  //if (!mWindow)
  //  return NS_OK;
  //NS_DispatchToMainThread(new StopRunnable(this));
  return NS_OK;
}

void
MediaEngineRemoteVideoSource::NotifyPull(MediaStreamGraph*,
                                         SourceMediaStream* aSource,
                                         TrackID aID, StreamTime aDesiredTime)
{
  VideoSegment segment;
  MonitorAutoLock mon(mMonitor);

  StreamTime delta = aDesiredTime - aSource->GetEndOfAppendedData(aID);
  if (delta > 0) {
    // nullptr images are allowed
    // gfx::IntSize size = image ? image->GetSize() : IntSize(0, 0);
    // segment.AppendFrame(image.forget().downcast<layers::Image>(), delta, size);
    // This can fail if either a) we haven't added the track yet, or b)
    // we've removed or finished the track.
    // aSource->AppendToTrack(aID, &(segment));
  }
}

}
