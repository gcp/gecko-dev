/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MEDIAENGINEREMOTEVIDEOSOURCE_H_
#define MEDIAENGINEREMOTEVIDEOSOURCE_H_

#include "prcvar.h"
#include "prthread.h"
#include "nsIThread.h"
#include "nsIRunnable.h"

#include "mozilla/Mutex.h"
#include "mozilla/Monitor.h"
#include "nsCOMPtr.h"
#include "nsThreadUtils.h"
#include "DOMMediaStream.h"
#include "nsDirectoryServiceDefs.h"
#include "nsComponentManagerUtils.h"

#include "VideoUtils.h"
#include "MediaEngineCameraVideoSource.h"
#include "VideoSegment.h"
#include "AudioSegment.h"
#include "StreamBuffer.h"
#include "MediaStreamGraph.h"

#include "MediaEngineWrapper.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"
// WebRTC library includes follow
#include "webrtc/common.h"
#include "webrtc/video_engine/include/vie_capture.h"

#include "NullTransport.h"

namespace mozilla {

/**
 * The WebRTC implementation of the MediaEngine interface.
 */
class MediaEngineRemoteVideoSource : public MediaEngineCameraVideoSource
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS

  //ViEExternalRenderer.
  //virtual int FrameSizeChange(unsigned int w, unsigned int h, unsigned int streams);
  //virtual int DeliverFrame(unsigned char* buffer,
  //                         int size,
  //                         uint32_t time_stamp,
  //                         int64_t render_time,
  //                         void *handle);
  //virtual bool IsTextureSupported() { return false; }

  MediaEngineRemoteVideoSource(int aIndex,
                               const char* aMonitorName = "RemoteVideo.Monitor");

  virtual nsresult Allocate(const VideoTrackConstraintsN& aConstraints,
                            const MediaEnginePrefs& aPrefs) MOZ_OVERRIDE;
  virtual nsresult Deallocate() MOZ_OVERRIDE;;
  virtual nsresult Start(SourceMediaStream*, TrackID) MOZ_OVERRIDE;
  virtual nsresult Stop(SourceMediaStream*, TrackID) MOZ_OVERRIDE;
  virtual void NotifyPull(MediaStreamGraph* aGraph,
                          SourceMediaStream* aSource,
                          TrackID aId,
                          StreamTime aDesiredTime) MOZ_OVERRIDE;
  virtual bool SatisfiesConstraintSets(
      const nsTArray<const dom::MediaTrackConstraintSet*>& aConstraintSets)
      MOZ_OVERRIDE;

protected:
  ~MediaEngineRemoteVideoSource() { Shutdown(); }

private:
  // Initialize the needed Video engine interfaces.
  void Init();
  void Shutdown();

  static bool SatisfiesConstraintSet(const dom::MediaTrackConstraintSet& aConstraints,
                                     const webrtc::CaptureCapability& aCandidate);
};

}

#endif /* NSMEDIAENGINEREMOTEVIDEOSOURCE_H_ */
