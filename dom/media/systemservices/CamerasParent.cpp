/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CamerasParent.h"
#include "CamerasUtils.h"
#include "MediaEngine.h"

#include "mozilla/Assertions.h"
#include "nsThreadUtils.h"
#include "prlog.h"

#undef LOG
#undef LOG_ENABLED
#if defined(PR_LOGGING)
PRLogModuleInfo *gCamerasParentLog;
#define LOG(args) PR_LOG(gCamerasParentLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gCamerasParentLog, 5)
#else
#define LOG(args)
#define LOG_ENABLED() (false)
#endif

namespace mozilla {
namespace camera {

static CamerasParent* sCamerasParent = nullptr;

class FrameSizeChangeRunnable : public nsRunnable {
public:
  FrameSizeChangeRunnable(CaptureEngine capEngine, int cap_id,
                          unsigned int aWidth, unsigned int aHeight)
    : mCapEngine(capEngine), mCapId(cap_id),
      mWidth(aWidth), mHeight(aHeight) {};

  NS_IMETHOD Run() {
    if (!sCamerasParent->SendFrameSizeChange(mCapEngine, mCapId,
                                             mWidth, mHeight)) {
      mResult = -1;
    } else {
      mResult = 0;
    }
    return NS_OK;
  }

  int GetResult() {
    return mResult;
  }

private:
  CaptureEngine mCapEngine;
  int mCapId;
  unsigned int mWidth;
  unsigned int mHeight;
  int mResult;
};

int
CallbackHelper::FrameSizeChange(unsigned int w, unsigned int h,
                                unsigned int streams)
{
  LOG(("Video FrameSizeChange: %ux%u", w, h));
  nsRefPtr<FrameSizeChangeRunnable> runnable =
    new FrameSizeChangeRunnable(mCapEngine, mCapturerId, w, h);
  sCamerasParent->GetBackgroundThread()->Dispatch(runnable, NS_DISPATCH_SYNC);
  return runnable->GetResult();
}

class DeliverFrameRunnable : public nsRunnable {
public:
  DeliverFrameRunnable(CaptureEngine engine,
                       int cap_id,
                       unsigned char* buffer,
                       int size,
                       uint32_t time_stamp,
                       int64_t ntp_time,
                       int64_t render_time)
    : mCapEngine(engine), mCapId(cap_id), mBuffer(buffer), mSize(size),
      mTimeStamp(time_stamp), mNtpTime(ntp_time), mRenderTime(render_time) {};

  NS_IMETHOD Run() {
    if (!sCamerasParent->DeliverFrameOverIPC(mCapEngine, mCapId,
                                             mBuffer, mSize, mTimeStamp,
                                             mNtpTime, mRenderTime)) {
      mResult = -1;
    } else {
      mResult = 0;
    }
    return NS_OK;
  }

  int GetResult() {
    return mResult;
  }

private:
  CaptureEngine mCapEngine;
  int mCapId;
  unsigned char* mBuffer;
  int mSize;
  uint32_t mTimeStamp;
  int64_t mNtpTime;
  int64_t mRenderTime;
  int mResult;
};

int
CamerasParent::DeliverFrameOverIPC(CaptureEngine cap_engine,
                                   int cap_id,
                                   unsigned char* buffer,
                                   int size,
                                   uint32_t time_stamp,
                                   int64_t ntp_time,
                                   int64_t render_time)
{
  if (!mShmemInitialized) {
    AllocShmem(size, SharedMemory::TYPE_BASIC, &mShmem);
    mShmemInitialized = true;
  }

  if (!mShmem.IsWritable()) {
    LOG(("Video shmem is not writeable in DeliverFrame"));
    // We can skip this frame if the buffer wasn't handed back
    // to us yet, it's not a real error.
    return 0;
  }

  // Prepare video buffer
  if (mShmem.Size<char>() != static_cast<size_t>(size)) {
    DeallocShmem(mShmem);
    // this may fail; always check return value
    if (!AllocShmem(size, SharedMemory::TYPE_BASIC, &mShmem)) {
      LOG(("Failure allocating new size video buffer"));
      return -1;
    }
  }

  // get() and Size() check for proper alignment of the segment
  memcpy(mShmem.get<char>(), buffer, size);

  if (!SendDeliverFrame(cap_engine, cap_id,
                        mShmem, size, time_stamp, ntp_time, render_time)) {
    return -1;
  }

  return 0;
}

int
CallbackHelper::DeliverFrame(unsigned char* buffer,
                             int size,
                             uint32_t time_stamp,
                             int64_t ntp_time,
                             int64_t render_time,
                             void *handle)
{
  LOG((__PRETTY_FUNCTION__));
  nsRefPtr<DeliverFrameRunnable> runnable =
    new DeliverFrameRunnable(mCapEngine, mCapturerId,
                             buffer, size, time_stamp, ntp_time, render_time);
  sCamerasParent->GetBackgroundThread()->Dispatch(runnable, NS_DISPATCH_SYNC);
  return runnable->GetResult();
}

bool
CamerasParent::RecvReleaseFrame(mozilla::ipc::Shmem&& s) {
  mShmem = s;
  return true;
}

bool
CamerasParent::RecvAllocateCaptureDevice(const int& aCapEngine,
                                         const nsCString& unique_id,
                                         int* numdev)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
    LOG(("Fails to initialize"));
    return false;
  }

  if (mPtrViECapture->AllocateCaptureDevice(unique_id.get(),
                                            MediaEngineSource::kMaxUniqueIdLength,
                                            *numdev)) {
    return false;
  }
  LOG(("Allocated device nr %d", *numdev));
  return true;
}

bool
CamerasParent::RecvReleaseCaptureDevice(const int& aCapEngine,
                                        const int &numdev)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
    LOG(("Fails to initialize"));
    return false;
  }

  LOG(("RecvReleaseCamera device nr %d", numdev));
  if (mPtrViECapture->ReleaseCaptureDevice(numdev)) {
    return false;
  }
  LOG(("Freed device nr %d", numdev));
  return true;
}

bool
CamerasParent::SetupEngine(CaptureEngine aCapEngine)
{
  webrtc::VideoEngine *engine = nullptr;

  switch (aCapEngine) {
    case ScreenEngine:
      if (!mScreenEngine) {
        mActiveConfig.Set<webrtc::CaptureDeviceInfo>(
          new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Screen));
        mScreenEngine = webrtc::VideoEngine::Create(mActiveConfig);
      }
      engine = mScreenEngine;
      break;
    case BrowserEngine:
      if (!mBrowserEngine) {
        mActiveConfig.Set<webrtc::CaptureDeviceInfo>(
          new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Browser));
        mBrowserEngine = webrtc::VideoEngine::Create(mActiveConfig);
      }
      engine = mBrowserEngine;
      break;
    case WinEngine:
      if (!mWinEngine) {
        mActiveConfig.Set<webrtc::CaptureDeviceInfo>(
          new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Window));
        mWinEngine = webrtc::VideoEngine::Create(mActiveConfig);
      }
      engine = mWinEngine;
      break;
    case AppEngine:
      if (!mAppEngine) {
        mActiveConfig.Set<webrtc::CaptureDeviceInfo>(
          new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Application));
        mAppEngine = webrtc::VideoEngine::Create(mActiveConfig);
      }
      engine = mAppEngine;
      break;
    case CameraEngine:
      if (!mCameraEngine) {
        mCameraEngine = webrtc::VideoEngine::Create();
      }
      engine = mCameraEngine;
      break;
    default:
      LOG(("Invalid webrtc Video engine"));
      MOZ_CRASH();
      break;
  }

  if (!engine) {
    LOG(("VideoEngine::Create failed"));
    return false;
  }

  mPtrViEBase = webrtc::ViEBase::GetInterface(engine);
  if (!mPtrViEBase) {
    LOG(("ViEBase::GetInterface failed"));
    return false;
  }

  if (mPtrViEBase->Init() < 0) {
    LOG(("ViEBase::Init failed"));
    return false;
  }

  mPtrViECapture = webrtc::ViECapture::GetInterface(engine);
  if (!mPtrViECapture) {
    LOG(("ViECapture::GetInterface failed"));
    return false;
  }

  mPtrViERender = webrtc::ViERender::GetInterface(engine);
  if (!mPtrViERender) {
    LOG(("ViERender::GetInterface failed"));
    return false;
  }

  mActiveEngine = aCapEngine;
  return true;
}

void
CamerasParent::CloseEngines()
{
  if (mPtrViERender) {
    mPtrViERender->Release();
    mPtrViERender = nullptr;
  }
  if (mPtrViECapture) {
    mPtrViECapture->Release();
    mPtrViECapture = nullptr;
  }
  if(mPtrViEBase) {
    mPtrViEBase->Release();
    mPtrViEBase = nullptr;
  }
}

bool
CamerasParent::EnsureInitialized(int aEngine)
{
  LOG((__PRETTY_FUNCTION__));
  CaptureEngine capEngine = static_cast<CaptureEngine>(aEngine);
  if (capEngine != mActiveEngine) {
    LOG(("Engine type new: %d old: %d", aEngine, mActiveEngine));
    CloseEngines();
    if (!SetupEngine(capEngine)) {
      return false;
    }
  }

  return true;
}

bool
CamerasParent::RecvNumberOfCaptureDevices(const int& aCapEngine,
                                          int* numdev)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
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
CamerasParent::RecvNumberOfCapabilities(const int& aCapEngine,
                                        const nsCString& unique_id,
                                        int* numCaps)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
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
CamerasParent::RecvGetCaptureCapability(const int &aCapEngine,
                                        const nsCString& unique_id,
                                        const int& num,
                                        CaptureCapability* capCapability)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
    LOG(("Fails to initialize"));
    return false;
  }

  LOG(("RecvGetCaptureCapability: %s %d", unique_id.get(), num));

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
CamerasParent::RecvGetCaptureDevice(const int& aCapEngine,
                                    const int& i,
                                    nsCString* aName,
                                    nsCString* aUniqueId)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
    LOG(("Fails to initialize"));
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
CamerasParent::RecvStartCapture(const int& aCapEngine,
                                const int& capnum,
                                const CaptureCapability& ipcCaps)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
    LOG(("Failure to initialize"));
    return false;
  }

  auto cbh = mCallbacks.AppendElement(
    new CallbackHelper(static_cast<CaptureEngine>(aCapEngine), capnum));
  auto render = static_cast<webrtc::ExternalRenderer*>(*cbh);

  int error = mPtrViERender->AddRenderer(capnum, webrtc::kVideoI420, render);
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
CamerasParent::RecvStopCapture(const int& aCapEngine,
                               const int& capnum)
{
  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
    LOG(("Failure to initialize"));
    return false;
  }

  mPtrViERender->StopRender(capnum);
  mPtrViERender->RemoveRenderer(capnum);
  mPtrViECapture->StopCapture(capnum);

  for (unsigned int i = 0; i < mCallbacks.Length(); i++) {
    if (mCallbacks[i]->mCapEngine == aCapEngine
        && mCallbacks[i]->mCapturerId == capnum) {
      delete mCallbacks[i];
      mCallbacks.RemoveElementAt(i);
      break;
    }
  }

  return true;
}

void
CamerasParent::ActorDestroy(ActorDestroyReason aWhy)
{
  // No more IPC from here
  LOG((__PRETTY_FUNCTION__));
}

CamerasParent::CamerasParent()
  : mActiveEngine(InvalidEngine),
    mPtrViEBase(nullptr),
    mPtrViECapture(nullptr),
    mPtrViERender(nullptr),
    mCameraEngine(nullptr),
    mScreenEngine(nullptr),
    mBrowserEngine(nullptr),
    mWinEngine(nullptr),
    mAppEngine(nullptr),
    mShmemInitialized(false)
{
#if defined(PR_LOGGING)
  if (!gCamerasParentLog)
    gCamerasParentLog = PR_NewLogModule("CamerasParent");
#endif
  LOG(("CamerasParent: %p", this));

  mPBackgroundThread = NS_GetCurrentThread();
  sCamerasParent = this;

  MOZ_COUNT_CTOR(CamerasParent);
}

CamerasParent::~CamerasParent()
{
  LOG(("~CamerasParent: %p", this));

  MOZ_COUNT_DTOR(CamerasParent);

  CloseEngines();

  if (mCameraEngine) {
    mCameraEngine->SetTraceCallback(nullptr);
    webrtc::VideoEngine::Delete(mCameraEngine);
  }
  if (mScreenEngine) {
    mScreenEngine->SetTraceCallback(nullptr);
    webrtc::VideoEngine::Delete(mScreenEngine);
  }
  if (mWinEngine) {
    mWinEngine->SetTraceCallback(nullptr);
    webrtc::VideoEngine::Delete(mWinEngine);
  }
  if (mBrowserEngine) {
    mBrowserEngine->SetTraceCallback(nullptr);
    webrtc::VideoEngine::Delete(mBrowserEngine);
  }
  if (mAppEngine) {
    mAppEngine->SetTraceCallback(nullptr);
    webrtc::VideoEngine::Delete(mAppEngine);
  }

  DeallocShmem(mShmem);

  mCameraEngine = nullptr;
  mScreenEngine = nullptr;
  mWinEngine = nullptr;
  mBrowserEngine = nullptr;
  mAppEngine = nullptr;

  for (unsigned int i = 0; i < mCallbacks.Length(); i++) {
    delete mCallbacks[i];
  }
}

PCamerasParent* CreateCamerasParent() {
  return new CamerasParent();
}

}
}
