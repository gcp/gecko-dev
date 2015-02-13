/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CamerasParent_h
#define mozilla_CamerasParent_h

#include "mozilla/dom/ContentParent.h"
#include "mozilla/camera/PCamerasParent.h"
#include "mozilla/ipc/Shmem.h"

// conflicts with #include of scoped_ptr.h
#undef FF
#include "webrtc/common.h"
// Video Engine
#include "webrtc/video_engine/include/vie_base.h"
#include "webrtc/video_engine/include/vie_capture.h"
#include "webrtc/video_engine/include/vie_render.h"
#include "CamerasChild.h"

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
  virtual bool RecvAllocateCaptureDevice(const int&, const nsCString&, int *) MOZ_OVERRIDE;
  virtual bool RecvReleaseCaptureDevice(const int&, const int &) MOZ_OVERRIDE;
  virtual bool RecvNumberOfCaptureDevices(const int&, int* numdev) MOZ_OVERRIDE;
  virtual bool RecvNumberOfCapabilities(const int&, const nsCString&, int*) MOZ_OVERRIDE;
  virtual bool RecvGetCaptureCapability(const int&, const nsCString&, const int&,
                                        CaptureCapability*) MOZ_OVERRIDE;
  virtual bool RecvGetCaptureDevice(const int&, const int&, nsCString*, nsCString*) MOZ_OVERRIDE;
  virtual bool RecvStartCapture(const int&, const int&, const CaptureCapability&) MOZ_OVERRIDE;
  virtual bool RecvStopCapture(const int&, const int&) MOZ_OVERRIDE;
  virtual bool RecvReleaseFrame(mozilla::ipc::Shmem&&) MOZ_OVERRIDE;
  virtual void ActorDestroy(ActorDestroyReason aWhy) MOZ_OVERRIDE;

  // forwarded to PBackground thread
  int DeliverFrameOverIPC(unsigned char* buffer,
                          int size,
                          uint32_t time_stamp,
                          int64_t render_time);


  CamerasParent();
  virtual ~CamerasParent();

protected:
  bool SetupEngine(CaptureEngine aCapEngine);
  void CloseActiveEngine();
  bool EnsureInitialized(int aEngine);

  // Kept active as long as the engine doesn't change
  CaptureEngine mActiveEngine;
  webrtc::Config mActiveConfig;
  webrtc::ViEBase *mPtrViEBase;
  webrtc::ViECapture *mPtrViECapture;
  webrtc::ViERender *mPtrViERender;

  webrtc::VideoEngine* mCameraEngine;
  webrtc::VideoEngine* mScreenEngine;
  webrtc::VideoEngine* mBrowserEngine;
  webrtc::VideoEngine* mWinEngine;
  webrtc::VideoEngine* mAppEngine;

  // Need this to avoid unneccesary WebRTC calls while enumerating.
  bool mCameraEngineInit;
  bool mScreenEngineInit;
  bool mBrowserEngineInit;
  bool mWinEngineInit;
  bool mAppEngineInit;

  // image buffer
  bool mShmemInitialized;
  mozilla::ipc::Shmem mShmem;

  // PBackground parent thread
  nsCOMPtr<nsIThread> mPBackgroundThread;
};

PCamerasParent* CreateCamerasParent();

} // namespace camera
} // namespace mozilla

#endif  // mozilla_CameraParent_h
