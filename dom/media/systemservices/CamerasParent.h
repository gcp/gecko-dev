/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CamerasParent_h
#define mozilla_CamerasParent_h

#include "mozilla/dom/ContentParent.h"
#include "mozilla/camera/PCamerasParent.h"

namespace webrtc {
  struct CaptureCapability;
  struct VideoEngine;
  struct ViEBase;
  struct ViECapture;
}

namespace mozilla {
namespace camera {

class CamerasParent :
  public PCamerasParent
{
public:
  virtual bool RecvEnumerateCameras() MOZ_OVERRIDE;
  virtual bool RecvAllocateCaptureDevice(const nsCString&, int *) MOZ_OVERRIDE;
  virtual bool RecvReleaseCaptureDevice(const int &) MOZ_OVERRIDE;
  virtual bool RecvNumberOfCaptureDevices(int* numdev) MOZ_OVERRIDE;
  virtual bool RecvNumberOfCapabilities(const nsCString&, int*) MOZ_OVERRIDE;
  virtual bool RecvGetCaptureCapability(const nsCString&, const int&,
                                        CaptureCapability*) MOZ_OVERRIDE;
  virtual bool RecvGetCaptureDevice(const int&, nsCString*, nsCString*) MOZ_OVERRIDE;
  virtual bool RecvStartCapture(const int&, const CaptureCapability&) MOZ_OVERRIDE;
  virtual bool RecvStopCapture() MOZ_OVERRIDE;
  virtual void ActorDestroy(ActorDestroyReason aWhy) MOZ_OVERRIDE;

  CamerasParent();
  virtual ~CamerasParent();

protected:
  bool InitVideoEngine();
  bool EnsureInitialized();

  webrtc::VideoEngine* mVideoEngine;
  bool mVideoEngineInit;
  webrtc::ViEBase *mPtrViEBase;
  webrtc::ViECapture *mPtrViECapture;
};

PCamerasParent* CreateCamerasParent();

} // namespace camera
} // namespace mozilla

#endif  // mozilla_CameraParent_h
