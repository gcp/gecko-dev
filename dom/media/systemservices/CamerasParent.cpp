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

int
CamerasParent::FrameSizeChange(unsigned int w, unsigned int h,
                               unsigned int streams)
{
  LOG(("Video FrameSizeChange: %ux%u", w, h));
  if (!SendFrameSizeChange(w, h)) {
    return -1;
  }
  return 0;
}

int
CamerasParent::DeliverFrame(unsigned char* buffer,
                            int size,
                            uint32_t time_stamp,
                            int64_t render_time,
                            void *handle)
{
  LOG((__FUNCTION__));
  if (!SendDeliverFrame(size, time_stamp, render_time)) {
    return -1;
  }
  return 0;
}

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
CamerasParent::RecvAllocateCaptureDevice(const nsCString& unique_id, int* numdev)
{
  LOG(("RecvAllocateCaptureDevice"));
  if (mPtrViECapture->AllocateCaptureDevice(unique_id.get(),
                                            MediaEngineSource::kMaxUniqueIdLength,
                                            *numdev)) {
    return false;
  }
  LOG(("Allocated device nr %d", *numdev));
  return true;
}

bool
CamerasParent::RecvReleaseCaptureDevice(const int &numdev)
{
  LOG(("RecvReleaseCamera device nr %d", numdev));
  if (mPtrViECapture->ReleaseCaptureDevice(numdev)) {
    return false;
  }
  LOG(("Freed device nr %d", numdev));
  return true;
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

  mPtrViERender = webrtc::ViERender::GetInterface(mVideoEngine);
  if (!mPtrViERender) {
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
CamerasParent::RecvGetCaptureCapability(const nsCString& unique_id, const int& num,
                                        CaptureCapability* capCapability)
{
  LOG(("RecvGetCaptureCapability: %s %d", unique_id.get(), num));
  if (!EnsureInitialized()) {
    LOG(("RecvGetCaptureCapability fails to initialize"));
    return false;
  }

  webrtc::CaptureCapability webrtcCaps;
  int error = mPtrViECapture->GetCaptureCapability(unique_id.get(),
                                                   MediaEngineSource::kMaxUniqueIdLength,
                                                   num,
                                                   webrtcCaps);
  if (error) {
    return false;
  }

  CaptureCapability capCap(webrtcCaps.width,
                           webrtcCaps.height,
                           webrtcCaps.maxFPS,
                           webrtcCaps.expectedCaptureDelay,
                           webrtcCaps.rawType,
                           webrtcCaps.codecType,
                           webrtcCaps.interlaced);
  LOG(("Capability: %d %d %d %d %d %d",
       webrtcCaps.width,
       webrtcCaps.height,
       webrtcCaps.maxFPS,
       webrtcCaps.expectedCaptureDelay,
       webrtcCaps.rawType,
       webrtcCaps.codecType));
  *capCapability = capCap;
  return true;
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

bool
CamerasParent::RecvStartCapture(const int& capnum,
                                const CaptureCapability& ipcCaps)
{
  if (!EnsureInitialized()) {
    LOG(("RecvStartCapture fails to initialize"));
    return false;
  }

  int error = mPtrViERender->AddRenderer(capnum, webrtc::kVideoI420, (webrtc::ExternalRenderer*)this);
  if (error == -1) {
    return false;
  }

  error = mPtrViERender->StartRender(capnum);
  if (error == -1) {
    return false;
  }

  webrtc::CaptureCapability capability;
  capability.width = ipcCaps.width();
  capability.height = ipcCaps.height();
  capability.maxFPS = ipcCaps.maxFPS();
  capability.expectedCaptureDelay = ipcCaps.expectedCaptureDelay();
  capability.rawType = static_cast<webrtc::RawVideoType>(ipcCaps.rawType());
  capability.codecType = static_cast<webrtc::VideoCodecType>(ipcCaps.codecType());
  capability.interlaced = ipcCaps.interlaced();

  if (mPtrViECapture->StartCapture(capnum, capability) < 0) {
    return false;
  }

  return true;
}

bool
CamerasParent::RecvStopCapture(const int& capnum)
{
  if (!EnsureInitialized()) {
    LOG(("RecvStopCapture fails to initialize"));
    return false;
  }

  mPtrViERender->StopRender(capnum);
  mPtrViERender->RemoveRenderer(capnum);
  mPtrViECapture->StopCapture(capnum);

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
