/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CamerasParent_h
#define mozilla_CamerasParent_h

#include "mozilla/dom/ContentParent.h"
#include "mozilla/camera/PCamerasParent.h"

// conflicts with #include of scoped_ptr.h
#undef FF
#include "webrtc/common.h"
// Video Engine
#include "webrtc/video_engine/include/vie_base.h"
#include "webrtc/video_engine/include/vie_capture.h"
#include "webrtc/video_engine/include/vie_render.h"

namespace mozilla {
namespace camera {

class CamerasParent :
  public PCamerasParent,
  public webrtc::ExternalRenderer
{
public:
  //ViEExternalRenderer.
  virtual int FrameSizeChange(unsigned int w, unsigned int h,
                              unsigned int streams) MOZ_OVERRIDE;
  virtual int DeliverFrame(unsigned char* buffer,
                           int size,
                           uint32_t time_stamp,
                           int64_t render_time,
                           void *handle) MOZ_OVERRIDE;
  virtual bool IsTextureSupported() MOZ_OVERRIDE { return false; };

  //
  virtual bool RecvEnumerateCameras() MOZ_OVERRIDE;
  virtual bool RecvAllocateCaptureDevice(const nsCString&, int *) MOZ_OVERRIDE;
  virtual bool RecvReleaseCaptureDevice(const int &) MOZ_OVERRIDE;
  virtual bool RecvNumberOfCaptureDevices(int* numdev) MOZ_OVERRIDE;
  virtual bool RecvNumberOfCapabilities(const nsCString&, int*) MOZ_OVERRIDE;
  virtual bool RecvGetCaptureCapability(const nsCString&, const int&,
                                        CaptureCapability*) MOZ_OVERRIDE;
  virtual bool RecvGetCaptureDevice(const int&, nsCString*, nsCString*) MOZ_OVERRIDE;
  virtual bool RecvStartCapture(const int&, const CaptureCapability&) MOZ_OVERRIDE;
  virtual bool RecvStopCapture(const int&) MOZ_OVERRIDE;
  virtual void ActorDestroy(ActorDestroyReason aWhy) MOZ_OVERRIDE;

  CamerasParent();
  virtual ~CamerasParent();

protected:
  bool InitVideoEngine();
  bool EnsureInitialized();

  webrtc::VideoEngine *mVideoEngine;
  bool mVideoEngineInit;
  webrtc::ViEBase *mPtrViEBase;
  webrtc::ViECapture *mPtrViECapture;
  webrtc::ViERender *mPtrViERender;
};

PCamerasParent* CreateCamerasParent();

} // namespace camera
} // namespace mozilla

#endif  // mozilla_CameraParent_h
