/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "CamerasParent.h"
#include "CamerasUtils.h"
#include "MediaEngine.h"
#include "prlog.h"

#include "webrtc/video_engine/include/vie_base.h"
#include "webrtc/video_engine/include/vie_capture.h"
#undef FF

PRLogModuleInfo *gCamerasParentLog;

#undef LOG
#undef LOG_ENABLED
#if defined(PR_LOGGING)
#define LOG(args) PR_LOG(gCamerasParentLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gCamerasParentLog, 5)
#else
#define LOG(args)
#define LOG_ENABLED() (false)
#endif

namespace mozilla {
namespace camera {

bool
CamerasParent::RecvEnumerateCameras()
{
  LOG(("RecvEnumerateCamereas()"));
  nsTArray<Camera> cameras;

  if (!SendCameraList(cameras)) {
    return false;
  }

  return true;
}

bool
CamerasParent::RecvAllocateCamera(bool* rv)
{
  LOG(("RecvAllocateCamera"));
  return false;
}

bool
CamerasParent::RecvReleaseCamera(bool* rv)
{
  LOG(("RecvReleaseCamera"));
  return false;
}

bool
CamerasParent::InitVideoEngine()
{
  mVideoEngine = webrtc::VideoEngine::Create();
  if (!mVideoEngine) {
    return false;
  }

  mPtrViEBase = webrtc::ViEBase::GetInterface(mVideoEngine);
  if (!mPtrViEBase) {
    return false;
  }

  if (mPtrViEBase->Init() < 0) {
    return false;
  }

  mPtrViECapture = webrtc::ViECapture::GetInterface(mVideoEngine);
  if (!mPtrViECapture) {
    return false;
  }

  mVideoEngineInit = true;
  return true;
}

bool
CamerasParent::EnsureInitialized()
{
  if (!mVideoEngineInit) {
    if (!InitVideoEngine()) {
      return false;
    }
  }

  return true;
}

bool
CamerasParent::RecvNumberOfCaptureDevices(int* numdev)
{
  LOG(("RecvNumberOfCaptureDevices"));
  if (!EnsureInitialized()) {
    *numdev = 0;
    LOG(("RecvNumberOfCaptureDevices fails to initialize"));
    return false;
  }

  int num = mPtrViECapture->NumberOfCaptureDevices();
  *numdev = num;
  if (num < 0) {
    LOG(("RecvNumberOfCaptureDevices couldn't find devices"));
    return false;
  } else {
    LOG(("RecvNumberOfCaptureDevices: %d", *numdev));
    return true;
  }
}

bool
CamerasParent::RecvNumberOfCapabilities(const nsCString& unique_id,
                                        int* numCaps)
{
  LOG(("RecvNumberOfCapabilities"));
  if (!EnsureInitialized()) {
    *numCaps = 0;
    LOG(("RecvNumberOfCapabilities fails to initialize"));
    return false;
  }

  LOG(("Getting caps for %s", unique_id.get()));
  int num = mPtrViECapture->NumberOfCapabilities(unique_id.get(),
                                                 MediaEngineSource::kMaxUniqueIdLength
                                                 );
  *numCaps = num;
  if (num < 0) {
    LOG(("RecvNumberOfCapabilities couldn't find capabilities"));
    return false;
  } else {
    LOG(("RecvNumberOfCapabilities: %d", *numCaps));
    return true;
  }

  return false;
}

bool
CamerasParent::RecvGetCaptureCapability(const nsCString&, const int&,
                                        CaptureCapability*)
{
  LOG(("RecvGetCaptureCapability"));
  return false;
}

bool
CamerasParent::RecvGetCaptureDevice(const int& i,
                                    nsCString* aName,
                                    nsCString* aUniqueId)
{
  if (!EnsureInitialized()) {
    LOG(("RecvGetCaptureDevice fails to initialize"));
    return false;
  }

  char deviceName[MediaEngineSource::kMaxDeviceNameLength];
  char uniqueId[MediaEngineSource::kMaxUniqueIdLength];

  LOG(("RecvGetCaptureDevice"));
  int error = mPtrViECapture->GetCaptureDevice(i,
                                               deviceName, sizeof(deviceName),
                                               uniqueId, sizeof(uniqueId));
  if (error) {
    LOG(("GetCaptureDevice failed: %d", error));
    return false;
  }

  LOG(("Returning %s name %s id", deviceName, uniqueId));

  aName->Assign(deviceName);
  aUniqueId->Assign(uniqueId);

  return true;
}

void
CamerasParent::ActorDestroy(ActorDestroyReason aWhy)
{
  // No more IPC from here
}

CamerasParent::CamerasParent()
  : mVideoEngine(nullptr), mVideoEngineInit(false),
    mPtrViEBase(nullptr), mPtrViECapture(nullptr)
{
#if defined(PR_LOGGING)
  if (!gCamerasParentLog)
    gCamerasParentLog = PR_NewLogModule("CamerasParent");
#endif
  LOG(("CamerasParent: %p", this));

  MOZ_COUNT_CTOR(CamerasParent);
}

CamerasParent::~CamerasParent()
{
  LOG(("~CamerasParent: %p", this));

  MOZ_COUNT_DTOR(CamerasParent);

  if(mPtrViEBase) {
    mPtrViEBase->Release();
    mPtrViEBase = nullptr;
  }
  if (mPtrViECapture) {
    mPtrViECapture->Release();
    mPtrViECapture = nullptr;
  }

  if (mVideoEngine) {
    mVideoEngine->SetTraceCallback(nullptr);
    webrtc::VideoEngine::Delete(mVideoEngine);
  }

  mVideoEngine = nullptr;
}

PCamerasParent* CreateCamerasParent() {
  return new CamerasParent();
}

}
}
