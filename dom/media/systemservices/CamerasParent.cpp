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

PRLogModuleInfo *gCamerasParentLog;
#define LOG(args) PR_LOG(gCamerasParentLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gCamerasParentLog, 5)

namespace mozilla {
namespace camera {

class FrameSizeChangeRunnable : public nsRunnable {
public:
  FrameSizeChangeRunnable(CamerasParent *aParent, CaptureEngine capEngine,
                          int cap_id, unsigned int aWidth, unsigned int aHeight)
    : mParent(aParent), mCapEngine(capEngine), mCapId(cap_id),
      mWidth(aWidth), mHeight(aHeight) {};

  NS_IMETHOD Run() {
    if (!mParent->ChildIsAlive()) {
      // Communication channel is being torn down
      LOG(("FrameSizeChangeRunnable is active without active Child"));
      mResult = 0;
      return NS_OK;
    }
    if (!mParent->SendFrameSizeChange(mCapEngine, mCapId, mWidth, mHeight)) {
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
  CamerasParent *mParent;
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
  LOG(("CallbackHelper Video FrameSizeChange: %ux%u", w, h));
  nsRefPtr<FrameSizeChangeRunnable> runnable =
    new FrameSizeChangeRunnable(mParent, mCapEngine, mCapturerId, w, h);
  MOZ_ASSERT(mParent);
  nsIThread * thread = mParent->GetBackgroundThread();
  MOZ_ASSERT(thread != nullptr);
  thread->Dispatch(runnable, NS_DISPATCH_NORMAL);
  return 0;
}

class DeliverFrameRunnable : public nsRunnable {
public:
  DeliverFrameRunnable(CamerasParent *aParent,
                       CaptureEngine engine,
                       int cap_id,
                       unsigned char* buffer,
                       int size,
                       uint32_t time_stamp,
                       int64_t ntp_time,
                       int64_t render_time)
    : mParent(aParent), mCapEngine(engine), mCapId(cap_id), mSize(size),
      mTimeStamp(time_stamp), mNtpTime(ntp_time), mRenderTime(render_time) {
    mBuffer = (unsigned char*)malloc(size);
    memcpy(mBuffer, buffer, size);
  };

  virtual ~DeliverFrameRunnable() {
    free(mBuffer);
  }

  NS_IMETHOD Run() {
    if (!mParent->ChildIsAlive()) {
      // Communication channel is being torn down
      mResult = 0;
      return NS_OK;
    }
    if (!mParent->DeliverFrameOverIPC(mCapEngine, mCapId,
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
  CamerasParent *mParent;
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
    LOG(("Initializing Shmem"));
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
    LOG(("Frame size change in Shmem"));
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
  //LOG((__PRETTY_FUNCTION__));
  nsRefPtr<DeliverFrameRunnable> runnable =
    new DeliverFrameRunnable(mParent, mCapEngine, mCapturerId,
                             buffer, size, time_stamp, ntp_time, render_time);
  MOZ_ASSERT(mParent);
  nsIThread * thread = mParent->GetBackgroundThread();
  MOZ_ASSERT(thread != nullptr);
  thread->Dispatch(runnable, NS_DISPATCH_NORMAL);
  return 0;
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

  if (mEngines[aCapEngine].mPtrViECapture->AllocateCaptureDevice(
      unique_id.get(), MediaEngineSource::kMaxUniqueIdLength, *numdev)) {
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
  if (mEngines[aCapEngine].mPtrViECapture->ReleaseCaptureDevice(numdev)) {
    return false;
  }
  LOG(("Freed device nr %d", numdev));
  return true;
}

bool
CamerasParent::SetupEngine(CaptureEngine aCapEngine)
{
  EngineHelper *helper = &mEngines[aCapEngine];

  // Already initialized
  if (helper->mEngine) {
    return true;
  }

  webrtc::CaptureDeviceInfo *captureDeviceInfo = nullptr;

  switch (aCapEngine) {
  case ScreenEngine:
    captureDeviceInfo =
      new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Screen);
    break;
  case BrowserEngine:
    captureDeviceInfo =
      new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Browser);
    break;
  case WinEngine:
    captureDeviceInfo =
      new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Window);
    break;
  case AppEngine:
    captureDeviceInfo =
      new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Application);
    break;
  case CameraEngine:
    captureDeviceInfo =
      new webrtc::CaptureDeviceInfo(webrtc::CaptureDeviceType::Camera);
    break;
  default:
    LOG(("Invalid webrtc Video engine"));
    MOZ_CRASH();
    break;
  }

  helper->mConfig.Set<webrtc::CaptureDeviceInfo>(captureDeviceInfo);
  helper->mEngine = webrtc::VideoEngine::Create(helper->mConfig);

  if (!helper->mEngine) {
    LOG(("VideoEngine::Create failed"));
    return false;
  }

  helper->mPtrViEBase = webrtc::ViEBase::GetInterface(helper->mEngine);
  if (!helper->mPtrViEBase) {
    LOG(("ViEBase::GetInterface failed"));
    return false;
  }

  if (helper->mPtrViEBase->Init() < 0) {
    LOG(("ViEBase::Init failed"));
    return false;
  }

  helper->mPtrViECapture = webrtc::ViECapture::GetInterface(helper->mEngine);
  if (!helper->mPtrViECapture) {
    LOG(("ViECapture::GetInterface failed"));
    return false;
  }

  helper->mPtrViERender = webrtc::ViERender::GetInterface(helper->mEngine);
  if (!helper->mPtrViERender) {
    LOG(("ViERender::GetInterface failed"));
    return false;
  }

  return true;
}

void
CamerasParent::CloseEngines()
{
  // Stop the callers
  while (mCallbacks.Length()) {
    auto capEngine = mCallbacks[0]->mCapEngine;
    auto capNum = mCallbacks[0]->mCapturerId;
    LOG(("Forcing shutdown of %d, %d", capEngine, capNum));
    RecvStopCapture(capEngine, capNum);
    RecvReleaseCaptureDevice(capEngine, capNum);
  }

  for (int i = 0; i < CaptureEngine::MaxEngine; i++) {
    if (mEngines[i].mEngineIsRunning) {
      LOG(("Being closed down while engine %d is running!", i));
    }
    if (mEngines[i].mPtrViERender) {
      mEngines[i].mPtrViERender->Release();
      mEngines[i].mPtrViERender = nullptr;
    }
    if (mEngines[i].mPtrViECapture) {
      mEngines[i].mPtrViECapture->Release();
      mEngines[i].mPtrViECapture = nullptr;
    }
    if(mEngines[i].mPtrViEBase) {
      mEngines[i].mPtrViEBase->Release();
      mEngines[i].mPtrViEBase = nullptr;
    }
  }
}

bool
CamerasParent::EnsureInitialized(int aEngine)
{
  LOG((__PRETTY_FUNCTION__));
  CaptureEngine capEngine = static_cast<CaptureEngine>(aEngine);
  if (!SetupEngine(capEngine)) {
    return false;
  }

  return true;
}

bool
CamerasParent::RecvNumberOfCaptureDevices(const int& aCapEngine)
{
  bool success = true;

  LOG((__PRETTY_FUNCTION__));
  if (!EnsureInitialized(aCapEngine)) {
    success &= SendReplyNumberOfCaptureDevices(0);
    LOG(("RecvNumberOfCaptureDevices fails to initialize"));
    return false;
  }

  int num = mEngines[aCapEngine].mPtrViECapture->NumberOfCaptureDevices();
  success &= SendReplyNumberOfCaptureDevices(num);

  if (num < 0) {
    LOG(("RecvNumberOfCaptureDevices couldn't find devices"));
    return false;
  } else {
    LOG(("RecvNumberOfCaptureDevices: %d %d", num, success));
    return success;
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
  int num =
    mEngines[aCapEngine].mPtrViECapture->NumberOfCapabilities(
      unique_id.get(),
      MediaEngineSource::kMaxUniqueIdLength);

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
  int error = mEngines[aCapEngine].mPtrViECapture->GetCaptureCapability(
      unique_id.get(), MediaEngineSource::kMaxUniqueIdLength,num, webrtcCaps);
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
  int error =
    mEngines[aCapEngine].mPtrViECapture->GetCaptureDevice(i,
                                                          deviceName,
                                                          sizeof(deviceName),
                                                          uniqueId,
                                                          sizeof(uniqueId));
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
    new CallbackHelper(static_cast<CaptureEngine>(aCapEngine), capnum, this));
  auto render = static_cast<webrtc::ExternalRenderer*>(*cbh);

  EngineHelper* helper = &mEngines[aCapEngine];

  int error =
    helper->mPtrViERender->AddRenderer(capnum, webrtc::kVideoI420, render);
  if (error == -1) {
    return false;
  }

  error = helper->mPtrViERender->StartRender(capnum);
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

  if (helper->mPtrViECapture->StartCapture(capnum, capability) < 0) {
    return false;
  }

  helper->mEngineIsRunning = true;
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

  mEngines[aCapEngine].mPtrViECapture->StopCapture(capnum);
  mEngines[aCapEngine].mPtrViERender->StopRender(capnum);
  mEngines[aCapEngine].mPtrViERender->RemoveRenderer(capnum);
  mEngines[aCapEngine].mEngineIsRunning = false;

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

void CamerasParent::DoShutdown()
{
  LOG((__PRETTY_FUNCTION__));
  CloseEngines();

  for (int i = 0; i < CaptureEngine::MaxEngine; i++) {
    if (mEngines[i].mEngine) {
      mEngines[i].mEngine->SetTraceCallback(nullptr);
      webrtc::VideoEngine::Delete(mEngines[i].mEngine);
      mEngines[i].mEngine = nullptr;
    }
  }

  if (mShmemInitialized) {
    DeallocShmem(mShmem);
    mShmemInitialized = false;
  }

  mPBackgroundThread = nullptr;
}

void
CamerasParent::ActorDestroy(ActorDestroyReason aWhy)
{
  // No more IPC from here
  LOG((__PRETTY_FUNCTION__));
  // We don't want to receive callbacks or anything if we can't
  // forward them anymore anyway.
  mChildIsAlive = false;
  CloseEngines();
}

CamerasParent::CamerasParent()
  : mShmemInitialized(false),
    mChildIsAlive(true)
{
  if (!gCamerasParentLog)
    gCamerasParentLog = PR_NewLogModule("CamerasParent");
  LOG(("CamerasParent: %p", this));

  mPBackgroundThread = NS_GetCurrentThread();
  MOZ_ASSERT(mPBackgroundThread != nullptr, "GetCurrentThread failed");

  MOZ_COUNT_CTOR(CamerasParent);
}

CamerasParent::~CamerasParent()
{
  LOG(("~CamerasParent: %p", this));

  MOZ_COUNT_DTOR(CamerasParent);
  DoShutdown();
}

PCamerasParent* CreateCamerasParent() {
  return new CamerasParent();
}

}
}
