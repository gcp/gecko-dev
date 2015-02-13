/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaEngineRemoteVideoSource.h"

#include "mozilla/RefPtr.h"
#include "VideoUtils.h"
#include "nsIPrefService.h"
#include "MediaTrackConstraints.h"
#include "CamerasChild.h"

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
  char deviceName[kMaxDeviceNameLength];
  char uniqueId[kMaxUniqueIdLength];
  if (mozilla::camera::GetCaptureDevice(mCaptureIndex,
                                        deviceName, kMaxDeviceNameLength,
                                        uniqueId, kMaxUniqueIdLength)) {
    return;
  }

  CopyUTF8toUTF16(deviceName, mDeviceName);
  CopyUTF8toUTF16(uniqueId, mUniqueId);
  return;
}

void
MediaEngineRemoteVideoSource::Shutdown() {
  // XXX: terminate PCameras / webrtc handles?
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
  LOG((__FUNCTION__));

  if (mozilla::camera::AllocateCaptureDevice(NS_ConvertUTF16toUTF8(mUniqueId).get(),
                                             kMaxUniqueIdLength, mCaptureIndex)) {
    return NS_ERROR_FAILURE;
  }
  mState = kAllocated;
  LOG(("Video device %d allocated", mCaptureIndex));

  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Deallocate()
{
  LOG((__FUNCTION__));
  if (mSources.IsEmpty()) {
    if (mState != kStopped && mState != kAllocated) {
      return NS_ERROR_FAILURE;
    }
    mozilla::camera::ReleaseCaptureDevice(mCaptureIndex);
    mState = kReleased;
    LOG(("Video device %d deallocated", mCaptureIndex));
  } else {
    LOG(("Video device %d deallocated but still in use", mCaptureIndex));
  }
  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Start(SourceMediaStream* aStream, TrackID aID)
{
  aStream->AddTrack(aID, 0, new VideoSegment());
  aStream->AdvanceKnownTracksTime(STREAM_TIME_MAX);
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
MediaEngineRemoteVideoSource::NotifyPull(MediaStreamGraph* aGraph,
                                         SourceMediaStream* aSource,
                                         TrackID aID, StreamTime aDesiredTime)
{
  VideoSegment segment;

  //MonitorAutoLock lock(mMonitor);
  StreamTime delta = aDesiredTime - aSource->GetEndOfAppendedData(aID);

  if (delta > 0) {
    // nullptr images are allowed
    AppendToTrack(aSource, mImage, aID, delta);
  }
}

typedef nsTArray<uint8_t> CapabilitySet;

bool
MediaEngineRemoteVideoSource::SatisfiesConstraintSet(const MediaTrackConstraintSet &aConstraints,
                                                     const webrtc::CaptureCapability& aCandidate) {
  if (!MediaEngineCameraVideoSource::IsWithin(aCandidate.width, aConstraints.mWidth) ||
      !MediaEngineCameraVideoSource::IsWithin(aCandidate.height, aConstraints.mHeight)) {
    return false;
  }
  if (!MediaEngineCameraVideoSource::IsWithin(aCandidate.maxFPS, aConstraints.mFrameRate)) {
    return false;
  }
  return true;
}

bool
MediaEngineRemoteVideoSource::SatisfiesConstraintSets(
      const nsTArray<const dom::MediaTrackConstraintSet*>& aConstraintSets)
{
  NS_ConvertUTF16toUTF8 uniqueId(mUniqueId);
  int num = mozilla::camera::NumberOfCapabilities(uniqueId.get());
  if (num <= 0) {
    return true;
  }

  CapabilitySet candidateSet;
  for (int i = 0; i < num; i++) {
    candidateSet.AppendElement(i);
  }

  for (size_t j = 0; j < aConstraintSets.Length(); j++) {
    for (size_t i = 0; i < candidateSet.Length();  ) {
      webrtc::CaptureCapability cap;
      mozilla::camera::GetCaptureCapability(uniqueId.get(), kMaxUniqueIdLength,
                                            candidateSet[i], cap);
      if (!SatisfiesConstraintSet(*aConstraintSets[j], cap)) {
        candidateSet.RemoveElementAt(i);
      } else {
        ++i;
      }
    }
  }
  return !!candidateSet.Length();
}

}
