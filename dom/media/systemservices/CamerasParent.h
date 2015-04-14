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

class CamerasParent;

class CallbackHelper : public webrtc::ExternalRenderer
{
public:
  CallbackHelper(CaptureEngine aCapEng, int aCapId)
    : mCapEngine(aCapEng), mCapturerId(aCapId) {};

  //ViEExternalRenderer.
  virtual int FrameSizeChange(unsigned int w, unsigned int h,
                              unsigned int streams) override;
  virtual int DeliverFrame(unsigned char* buffer,
                           int size,
                           uint32_t time_stamp,
                           int64_t ntp_time,
                           int64_t render_time,
                           void *handle) override;
  virtual bool IsTextureSupported() override { return false; };

  friend CamerasParent;

private:
  CaptureEngine mCapEngine;
  int mCapturerId;
};

class CamerasParent :  public PCamerasParent
{
public:
  //
  virtual bool RecvAllocateCaptureDevice(const int&, const nsCString&, int *) override;
  virtual bool RecvReleaseCaptureDevice(const int&, const int &) override;
  virtual bool RecvNumberOfCaptureDevices(const int&, int* numdev) override;
  virtual bool RecvNumberOfCapabilities(const int&, const nsCString&, int*) override;
  virtual bool RecvGetCaptureCapability(const int&, const nsCString&, const int&,
                                        CaptureCapability*) override;
  virtual bool RecvGetCaptureDevice(const int&, const int&, nsCString*, nsCString*) override;
  virtual bool RecvStartCapture(const int&, const int&, const CaptureCapability&) override;
  virtual bool RecvStopCapture(const int&, const int&) override;
  virtual bool RecvReleaseFrame(mozilla::ipc::Shmem&&) override;
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  nsCOMPtr<nsIThread> GetBackgroundThread() { return mPBackgroundThread; };

  // forwarded to PBackground thread
  int DeliverFrameOverIPC(CaptureEngine capEng,
                          int cap_id,
                          unsigned char* buffer,
                          int size,
                          uint32_t time_stamp,
                          int64_t ntp_time,
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

  nsTArray<CallbackHelper*> mCallbacks;

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
