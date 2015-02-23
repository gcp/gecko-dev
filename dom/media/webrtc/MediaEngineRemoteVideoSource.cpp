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

#ifdef PR_LOGGING
extern PRLogModuleInfo* GetMediaManagerLog();
#define LOG(msg) PR_LOG(GetMediaManagerLog(), PR_LOG_DEBUG, msg)
#define LOGFRAME(msg) PR_LOG(GetMediaManagerLog(), 6, msg)
#else
#define LOG(msg)
#define LOGFRAME(msg)
#endif

namespace mozilla {

using dom::ConstrainLongRange;

NS_IMPL_ISUPPORTS0(MediaEngineRemoteVideoSource)

MediaEngineRemoteVideoSource::MediaEngineRemoteVideoSource(
  int aIndex, mozilla::camera::CaptureEngine aCapEngine,
  dom::MediaSourceEnum aMediaSource, const char* aMonitorName)
  : MediaEngineCameraVideoSource(aIndex, aMonitorName),
    mMediaSource(aMediaSource),
    mCapEngine(aCapEngine)
{
  Init();
}

void
MediaEngineRemoteVideoSource::Init() {
  LOG((__PRETTY_FUNCTION__));
  char deviceName[kMaxDeviceNameLength];
  char uniqueId[kMaxUniqueIdLength];
  if (mozilla::camera::GetCaptureDevice(mCapEngine,
                                        mCaptureIndex,
                                        deviceName, kMaxDeviceNameLength,
                                        uniqueId, kMaxUniqueIdLength)) {
    return;
  }

  CopyUTF8toUTF16(deviceName, mDeviceName);
  CopyUTF8toUTF16(uniqueId, mUniqueId);

  mInitDone = true;

  return;
}

void
MediaEngineRemoteVideoSource::Shutdown() {
  LOG((__PRETTY_FUNCTION__));
  mozilla::camera::Shutdown();
  return;
}

nsresult
MediaEngineRemoteVideoSource::Allocate(const VideoTrackConstraintsN& aConstraints,
                                       const MediaEnginePrefs& aPrefs)
{
  LOG((__PRETTY_FUNCTION__));

  ChooseCapability(aConstraints, aPrefs);
  if (mozilla::camera::AllocateCaptureDevice(mCapEngine,
                                             NS_ConvertUTF16toUTF8(mUniqueId).get(),
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
    mozilla::camera::ReleaseCaptureDevice(mCapEngine, mCaptureIndex);
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
  LOG((__PRETTY_FUNCTION__));
  if (!mInitDone || !aStream) {
    LOG(("No stream or init not done"));
    return NS_ERROR_FAILURE;
  }

  mSources.AppendElement(aStream);

  aStream->AddTrack(aID, 0, new VideoSegment(), SourceMediaStream::ADDTRACK_QUEUED);

  if (mState == kStarted) {
    LOG(("State is not started"));
    return NS_OK;
  }
  mImageContainer = layers::LayerManager::CreateImageContainer();

  mState = kStarted;
  mTrackID = aID;

  if (mozilla::camera::StartCapture(mCapEngine,
                                    mCaptureIndex, mCapability, this)) {
    LOG(("StartCapture failed"));
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult
MediaEngineRemoteVideoSource::Stop(mozilla::SourceMediaStream* aSource,
                                   mozilla::TrackID aID)
{
  LOG((__PRETTY_FUNCTION__));
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
    MonitorAutoLock lock(mMonitor);
    mState = kStopped;
    // Drop any cached image so we don't start with a stale image on next
    // usage
    mImage = nullptr;
  }

  mozilla::camera::StopCapture(mCapEngine, mCaptureIndex);

  return NS_OK;
}

void
MediaEngineRemoteVideoSource::NotifyPull(MediaStreamGraph* aGraph,
                                         SourceMediaStream* aSource,
                                         TrackID aID, StreamTime aDesiredTime)
{
  VideoSegment segment;

  MonitorAutoLock lock(mMonitor);
  StreamTime delta = aDesiredTime - aSource->GetEndOfAppendedData(aID);

  if (delta > 0) {
    // nullptr images are allowed
    AppendToTrack(aSource, mImage, aID, delta);
  }
}

int
MediaEngineRemoteVideoSource::FrameSizeChange(unsigned int w, unsigned int h,
                                              unsigned int streams)
{
  mWidth = w;
  mHeight = h;
  LOG(("Video FrameSizeChange: %ux%u", w, h));
  return 0;
}

int
MediaEngineRemoteVideoSource::DeliverFrame(unsigned char* buffer,
                                           int size,
                                           uint32_t time_stamp,
                                           int64_t ntp_time,
                                           int64_t render_time,
                                           void *handle)
{
  // Check for proper state.
  if (mState != kStarted) {
    LOG(("DeliverFrame: video not started"));
    return 0;
  }

  if (mWidth*mHeight + 2*(((mWidth+1)/2)*((mHeight+1)/2)) != size) {
    MOZ_ASSERT(false, "Wrong size frame in DeliverFrame!");
    return 0;
  }

  // Create a video frame and append it to the track.
  nsRefPtr<layers::Image> image = mImageContainer->CreateImage(ImageFormat::PLANAR_YCBCR);
  layers::PlanarYCbCrImage* videoImage = static_cast<layers::PlanarYCbCrImage*>(image.get());

  uint8_t* frame = static_cast<uint8_t*> (buffer);
  const uint8_t lumaBpp = 8;
  const uint8_t chromaBpp = 4;

  // Take lots of care to round up!
  layers::PlanarYCbCrData data;
  data.mYChannel = frame;
  data.mYSize = IntSize(mWidth, mHeight);
  data.mYStride = (mWidth * lumaBpp + 7)/ 8;
  data.mCbCrStride = (mWidth * chromaBpp + 7) / 8;
  data.mCbChannel = frame + mHeight * data.mYStride;
  data.mCrChannel = data.mCbChannel + ((mHeight+1)/2) * data.mCbCrStride;
  data.mCbCrSize = IntSize((mWidth+1)/ 2, (mHeight+1)/ 2);
  data.mPicX = 0;
  data.mPicY = 0;
  data.mPicSize = IntSize(mWidth, mHeight);
  data.mStereoMode = StereoMode::MONO;

  videoImage->SetData(data);

#ifdef DEBUG
  static uint32_t frame_num = 0;
  LOGFRAME(("frame %d (%dx%d); timestamp %u, ntp_time %lu, render_time %lu", frame_num++,
            mWidth, mHeight, time_stamp, ntp_time, render_time));
#endif

  // we don't touch anything in 'this' until here (except for snapshot,
  // which has it's own lock)
  MonitorAutoLock lock(mMonitor);

  // implicitly releases last image
  mImage = image.forget();

  // Push the frame into the MSG with a minimal duration.  This will likely
  // mean we'll still get NotifyPull calls which will then return the same
  // frame again with a longer duration.  However, this means we won't
  // fail to get the frame in and drop frames.

  // XXX The timestamp for the frame should be based on the Capture time,
  // not the MSG time, and MSG should never, ever block on a (realtime)
  // video frame (or even really for streaming - audio yes, video probably no).
  // Note that MediaPipeline currently ignores the timestamps from MSG
  uint32_t len = mSources.Length();
  for (uint32_t i = 0; i < len; i++) {
    if (mSources[i]) {
      AppendToTrack(mSources[i], mImage, mTrackID, 1); // shortest possible duration
    }
  }

  return 0;
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
  int num = mozilla::camera::NumberOfCapabilities(mCapEngine, uniqueId.get());
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
      mozilla::camera::GetCaptureCapability(mCapEngine,
                                            uniqueId.get(),
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
  int num = mozilla::camera::NumberOfCapabilities(mCapEngine, uniqueId.get());
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
    mozilla::camera::GetCaptureCapability(mCapEngine,
                                          uniqueId.get(),
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
        mozilla::camera::GetCaptureCapability(mCapEngine,
                                              uniqueId.get(),
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
    mozilla::camera::GetCaptureCapability(mCapEngine,
                                          NS_ConvertUTF16toUTF8(mUniqueId).get(),
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

void MediaEngineRemoteVideoSource::Refresh(int aIndex) {
  // NOTE: mCaptureIndex might have changed when allocated!
  // Use aIndex to update information, but don't change mCaptureIndex!!
  // Caller looked up this source by uniqueId, so it shouldn't change
  char deviceName[kMaxDeviceNameLength];
  char uniqueId[kMaxUniqueIdLength];

  if (mozilla::camera::GetCaptureDevice(mCapEngine,
                                        aIndex,
                                        deviceName, sizeof(deviceName),
                                        uniqueId, sizeof(uniqueId))) {
    return;
  }

  CopyUTF8toUTF16(deviceName, mDeviceName);
#ifdef DEBUG
  nsString temp;
  CopyUTF8toUTF16(uniqueId, temp);
  MOZ_ASSERT(temp.Equals(mUniqueId));
#endif
}

}
