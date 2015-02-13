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
  LOG((__FUNCTION__));

  ChooseCapability(aConstraints, aPrefs);
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
  LOG((__FUNCTION__));
  if (!mInitDone || !aStream) {
    return NS_ERROR_FAILURE;
  }

  mSources.AppendElement(aStream);

  aStream->AddTrack(aID, 0, new VideoSegment());
  aStream->AdvanceKnownTracksTime(STREAM_TIME_MAX);

  if (mState == kStarted) {
    return NS_OK;
  }
  mImageContainer = layers::LayerManager::CreateImageContainer();

  mState = kStarted;
  mTrackID = aID;

  //int error = mViERender->AddRenderer(mCaptureIndex, webrtc::kVideoI420, (webrtc::ExternalRenderer*)this);
  //if (error == -1) {
  //  return NS_ERROR_FAILURE;
  //}

  //error = mViERender->StartRender(mCaptureIndex);
  //if (error == -1) {
  //  return NS_ERROR_FAILURE;
  //}

  //if (mViECapture->StartCapture(mCaptureIndex, mCapability) < 0) {
  //  return NS_ERROR_FAILURE;
  //}

  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Stop(mozilla::SourceMediaStream* aSource,
                                   mozilla::TrackID aID)
{
  LOG((__FUNCTION__));
  if (!mSources.RemoveElement(aSource)) {
    // Already stopped - this is allowed
    return NS_OK;
  }

  aSource->EndTrack(aID);

  if (!mSources.IsEmpty()) {
    return NS_OK;
  }
  if (mState != kStarted) {
    return NS_ERROR_FAILURE;
  }

  {
    //MonitorAutoLock lock(mMonitor);
    mState = kStopped;
    // Drop any cached image so we don't start with a stale image on next
    // usage
    mImage = nullptr;
  }
  //mViERender->StopRender(mCaptureIndex);
  //mViERender->RemoveRenderer(mCaptureIndex);
  //mViECapture->StopCapture(mCaptureIndex);

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
      mozilla::camera::GetCaptureCapability(uniqueId.get(),
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

void
MediaEngineRemoteVideoSource::ChooseCapability(
    const VideoTrackConstraintsN &aConstraints,
    const MediaEnginePrefs &aPrefs)
{
  NS_ConvertUTF16toUTF8 uniqueId(mUniqueId);
  int num = mozilla::camera::NumberOfCapabilities(uniqueId.get());
  if (num <= 0) {
    // Mac doesn't support capabilities.
    return GuessCapability(aConstraints, aPrefs);
  }

  // The rest is the full algorithm for cameras that can list their capabilities.

  LOG(("ChooseCapability: prefs: %dx%d @%d-%dfps",
       aPrefs.mWidth, aPrefs.mHeight, aPrefs.mFPS, aPrefs.mMinFPS));

  CapabilitySet candidateSet;
  for (int i = 0; i < num; i++) {
    candidateSet.AppendElement(i);
  }

  // Pick among capabilities: First apply required constraints.

  for (uint32_t i = 0; i < candidateSet.Length();) {
    webrtc::CaptureCapability cap;
    mozilla::camera::GetCaptureCapability(uniqueId.get(),
                                          candidateSet[i], cap);
    if (!SatisfiesConstraintSet(aConstraints.mRequired, cap)) {
      candidateSet.RemoveElementAt(i);
    } else {
      ++i;
    }
  }

  CapabilitySet tailSet;

  // Then apply advanced (formerly known as optional) constraints.

  if (aConstraints.mAdvanced.WasPassed()) {
    auto &array = aConstraints.mAdvanced.Value();

    for (uint32_t i = 0; i < array.Length(); i++) {
      CapabilitySet rejects;
      for (uint32_t j = 0; j < candidateSet.Length();) {
        webrtc::CaptureCapability cap;
        mozilla::camera::GetCaptureCapability(uniqueId.get(),
                                              candidateSet[j], cap);
        if (!SatisfiesConstraintSet(array[i], cap)) {
          rejects.AppendElement(candidateSet[j]);
          candidateSet.RemoveElementAt(j);
        } else {
          ++j;
        }
      }
      (candidateSet.Length()? tailSet : candidateSet).MoveElementsFrom(rejects);
    }
  }

  if (!candidateSet.Length()) {
    candidateSet.AppendElement(0);
  }

  int prefWidth = aPrefs.GetWidth();
  int prefHeight = aPrefs.GetHeight();

  // Default is closest to available capability but equal to or below;
  // otherwise closest above.  Since we handle the num=0 case above and
  // take the first entry always, we can never exit uninitialized.

  webrtc::CaptureCapability cap;
  bool higher = true;
  for (uint32_t i = 0; i < candidateSet.Length(); i++) {
    mozilla::camera::GetCaptureCapability(NS_ConvertUTF16toUTF8(mUniqueId).get(),
                                          candidateSet[i], cap);
    if (higher) {
      if (i == 0 ||
          (mCapability.width > cap.width && mCapability.height > cap.height)) {
        // closer than the current choice
        mCapability = cap;
        // FIXME: expose expected capture delay?
      }
      if (cap.width <= (uint32_t) prefWidth && cap.height <= (uint32_t) prefHeight) {
        higher = false;
      }
    } else {
      if (cap.width > (uint32_t) prefWidth || cap.height > (uint32_t) prefHeight ||
          cap.maxFPS < (uint32_t) aPrefs.mMinFPS) {
        continue;
      }
      if (mCapability.width < cap.width && mCapability.height < cap.height) {
        mCapability = cap;
        // FIXME: expose expected capture delay?
      }
    }
    // Same resolution, maybe better format or FPS match
    if (mCapability.width == cap.width && mCapability.height == cap.height) {
      // FPS too low
      if (cap.maxFPS < (uint32_t) aPrefs.mMinFPS) {
        continue;
      }
      // Better match
      if (cap.maxFPS < mCapability.maxFPS) {
        mCapability = cap;
      } else if (cap.maxFPS == mCapability.maxFPS) {
        // Resolution and FPS the same, check format
        if (cap.rawType == webrtc::RawVideoType::kVideoI420
          || cap.rawType == webrtc::RawVideoType::kVideoYUY2
          || cap.rawType == webrtc::RawVideoType::kVideoYV12) {
          mCapability = cap;
        }
      }
    }
  }
  LOG(("chose cap %dx%d @%dfps codec %d raw %d",
       mCapability.width, mCapability.height, mCapability.maxFPS,
       mCapability.codecType, mCapability.rawType));
}

}
