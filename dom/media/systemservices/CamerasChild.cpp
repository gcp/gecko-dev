/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CamerasChild.h"
#include "CamerasUtils.h"

#include "webrtc/video_engine/include/vie_capture.h"
#undef FF

#include "mozilla/Assertions.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/Mutex.h"
#include "mozilla/SyncRunnable.h"
#include "xpcom/glue/nsThreadUtils.h"
#include "prlog.h"

#undef LOG
#undef LOG_ENABLED
PRLogModuleInfo *gCamerasChildLog;
#define LOG(args) PR_LOG(gCamerasChildLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gCamerasChildLog, 5)

namespace mozilla {
namespace camera {

/*class CamerasThreadDestructor : public nsRunnable
{
public:
  explicit CamerasThreadDestructor(nsIThread *aThread)
    : mThread(aThread) {}

  NS_IMETHOD Run() override
  {
    MOZ_ASSERT(NS_IsMainThread());
    if (mThread) {
      mThread->Shutdown();
    }
    return NS_OK;
  }

private:
  ~CamerasThreadDestructor() {}
  nsCOMPtr<nsIThread> mThread;
};*/

class CamerasSingleton {
public:
  CamerasSingleton()
    : mCamerasMutex("CamerasSingleton::mCamerasMutex"),
      mCameras(nullptr),
      mCamerasChildThread(nullptr) {
#if defined(PR_LOGGING)
    if (!gCamerasChildLog)
      gCamerasChildLog = PR_NewLogModule("CamerasChild");
#endif
    LOG(("CamerasSingleton: %p", this));
  }

  ~CamerasSingleton() {
    mCameras = nullptr;
    // We're off the main thread so we can spin the event loop here.
    MOZ_ASSERT(!NS_IsMainThread());
    if (mCamerasChildThread) {
      mCamerasChildThread->Shutdown();
      mCamerasChildThread = nullptr;
    }
    LOG(("~CamerasSingleton: %p", this));
  }

  static CamerasSingleton& getInstance() {
    static CamerasSingleton instance;
    return instance;
  }

  static OffTheBooksMutex& getMutex() {
    return getInstance().mCamerasMutex;
  }

  static CamerasChild*& getChild() {
    getInstance().getMutex().AssertCurrentThreadOwns();
    return getInstance().mCameras;
  }

  static nsIThread*& getThread() {
    getInstance().getMutex().AssertCurrentThreadOwns();
    return getInstance().mCamerasChildThread;
  }

  // We will be alive on destruction.
  mozilla::OffTheBooksMutex mCamerasMutex;

private:
  CamerasChild *mCameras;
  nsCOMPtr<nsIThread> mCamerasChildThread;
};

class AttachPBackgroundToIPCThread : public nsRunnable
{
public:
  explicit AttachPBackgroundToIPCThread()
    : mBackgroundChild(nullptr) {}

  NS_IMETHOD Run() override
  {
    // Try to get the PBackground handle
    ipc::PBackgroundChild* existingBackgroundChild =
      ipc::BackgroundChild::GetForCurrentThread();
    // If it's not spun up yet, block until it is, and retry
    if (!existingBackgroundChild) {
      LOG(("No existingBackgroundChild"));
      SynchronouslyCreatePBackground();
      existingBackgroundChild =
        ipc::BackgroundChild::GetForCurrentThread();
    }
    // By now PBackground is guaranteed to be up
    MOZ_RELEASE_ASSERT(existingBackgroundChild);
    mBackgroundChild = existingBackgroundChild;

    return NS_OK;
  }

  ipc::PBackgroundChild* GetBackgroundChild() {
    return mBackgroundChild;
  }

private:
  ipc::PBackgroundChild* mBackgroundChild;
}

static CamerasChild* Cameras(bool trace) {
  OffTheBooksMutexAutoLock lock(CamerasSingleton::getMutex());
  if (!gCamerasChildLog)
    gCamerasChildLog = PR_NewLogModule("CamerasChild");
  if (!CamerasSingleton::getChild()) {
    LOG(("No sCameras, setting up"));
    if (!CamerasSingleton::getThread()) {
      LOG(("Spinning up IPC Thread"));
      nsresult rv = NS_NewNamedThread("Cameras IPC",
        getter_AddRefs(CamerasSingleton::getThread()));
      if (NS_FAILED(rv)) {
        LOG(("Error launching IPC Thread"));
        return nullptr;
      }
    }
    AttachPBackgroundToIPCThread* runnable = new AttachPBackgroundToIPCThread();
    SyncRunnable::DispatchToThread(CamerasSingleton::getThread(), runnable);
    ipc::PBackgroundChild* backgroundChild = runnable->GetBackgroundChild();

    // Create PCameras by sending a message to the parent
    CamerasSingleton::getChild() =
      static_cast<CamerasChild*>(backgroundChild->SendPCamerasConstructor());
  }
  if (trace) {
    CamerasChild* tmp = CamerasSingleton::getChild();
    LOG(("Returning sCameras: %p", tmp));
  }
  MOZ_ASSERT(CamerasSingleton::getChild());
  return CamerasSingleton::getChild();
}

int NumberOfCapabilities(CaptureEngine aCapEngine, const char* deviceUniqueIdUTF8)
{
  return Cameras(true)->NumberOfCapabilities(aCapEngine, deviceUniqueIdUTF8);
}

bool
CamerasChild::RecvReplyNumberOfCapabilities(const int& numdev)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplyInteger = numdev;
  monitor.Notify();
  return true;
}

int
CamerasChild::NumberOfCapabilities(CaptureEngine aCapEngine,
                                   const char* deviceUniqueIdUTF8)
{
  LOG((__PRETTY_FUNCTION__));
  LOG(("NumberOfCapabilities for %s", deviceUniqueIdUTF8));
  MonitorAutoLock monitor(mReplyMonitor);
  nsCString unique_id(deviceUniqueIdUTF8);
  nsRefPtr<nsIRunnable> runnable =
    NS_NewRunnableMethodWithArgs<CaptureEngine>(
      this, &CamerasChild::SendNumberOfCapabilities,
      aCapEngine, unique_id);
  getThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  monitor.Wait();
  LOG(("Capture capability count: %d", mReplyInteger));
  return mReplyInteger;
}

int NumberOfCaptureDevices(CaptureEngine aCapEngine)
{
  return Cameras(true)->NumberOfCaptureDevices(aCapEngine);
}

int
CamerasChild::NumberOfCaptureDevices(CaptureEngine aCapEngine)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  nsRefPtr<nsIRunnable> runnable =
    NS_NewRunnableMethodWithArgs<CaptureEngine>(
      this, &CamerasChild::SendNumberOfCaptureDevices, aCapEngine);
  getThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  monitor.Wait();
  // Note: This is typically the first call, so there's no guarantee
  // gLog is initialized yet before the Cameras() call.
  LOG(("Capture Devices: %d", mReplyInteger));
  return mReplyInteger;
}

bool
CamerasChild::RecvReplyNumberOfCaptureDevices(const int& numdev)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplyInteger = numdev;
  monitor.Notify();
  return true;
}

int GetCaptureCapability(CaptureEngine aCapEngine, const char* unique_idUTF8,
                         const unsigned int capability_number,
                         webrtc::CaptureCapability& capability)
{
  LOG(("GetCaptureCapability: %s %d", unique_idUTF8, capability_number));
  nsCString unique_id(unique_idUTF8);
  CaptureCapability ipcCapability;
  Cameras(true)->SendGetCaptureCapability(aCapEngine,
                                      unique_id,
                                      capability_number, &ipcCapability);
  capability.width = ipcCapability.width();
  capability.height = ipcCapability.height();
  capability.maxFPS = ipcCapability.maxFPS();
  capability.expectedCaptureDelay = ipcCapability.expectedCaptureDelay();
  capability.rawType = static_cast<webrtc::RawVideoType>(ipcCapability.rawType());
  capability.codecType = static_cast<webrtc::VideoCodecType>(ipcCapability.codecType());
  capability.interlaced = ipcCapability.interlaced();
  return 0;
}


int GetCaptureDevice(CaptureEngine aCapEngine,
                     unsigned int list_number, char* device_nameUTF8,
                     const unsigned int device_nameUTF8Length,
                     char* unique_idUTF8,
                     const unsigned int unique_idUTF8Length)
{
  LOG((__PRETTY_FUNCTION__));
  nsCString device_name;
  nsCString unique_id;
  if (Cameras(true)->SendGetCaptureDevice(aCapEngine,
                                      list_number, &device_name, &unique_id)) {
    base::strlcpy(device_nameUTF8, device_name.get(), device_nameUTF8Length);
    base::strlcpy(unique_idUTF8, unique_id.get(), unique_idUTF8Length);
    LOG(("Got %s name %s id", device_nameUTF8, unique_idUTF8));
    return 0;
  } else {
    return -1;
  }
}

int AllocateCaptureDevice(CaptureEngine aCapEngine,
                          const char* unique_idUTF8,
                          const unsigned int unique_idUTF8Length,
                          int& capture_id)
{
  LOG((__PRETTY_FUNCTION__));
  nsCString unique_id(unique_idUTF8);
  if (Cameras(true)->SendAllocateCaptureDevice(aCapEngine,
                                           unique_id, &capture_id)) {
    LOG(("Success allocating %s %d", unique_idUTF8, capture_id));
    return 0;
  } else {
    LOG(("Failure allocating capture device %s %d", unique_idUTF8, capture_id));
    return -1;
  }
}

int ReleaseCaptureDevice(CaptureEngine aCapEngine, const int capture_id)
{
  LOG((__PRETTY_FUNCTION__));
  if (Cameras(true)->SendReleaseCaptureDevice(aCapEngine, capture_id)) {
    return 0;
  } else {
    return -1;
  }
}

void
CamerasChild::AddCallback(const CaptureEngine aCapEngine, const int capture_id,
                          webrtc::ExternalRenderer* render)
{
  MutexAutoLock lock(mCallbackMutex);
  CapturerElement ce;
  ce.engine = aCapEngine;
  ce.id = capture_id;
  ce.callback = render;
  mCallbacks.AppendElement(ce);
}

void
CamerasChild::RemoveCallback(const CaptureEngine aCapEngine, const int capture_id)
{
  MutexAutoLock lock(mCallbackMutex);
  for (unsigned int i = 0; i < mCallbacks.Length(); i++) {
    CapturerElement ce = mCallbacks[i];
    if (ce.engine == aCapEngine && ce.id == capture_id) {
      mCallbacks.RemoveElementAt(i);
      break;
    }
  }
}

int StartCapture(CaptureEngine aCapEngine,
                 const int capture_id,
                 webrtc::CaptureCapability& webrtcCaps,
                 webrtc::ExternalRenderer* cb)
{
  Cameras(true)->StartCapture(aCapEngine,
                              capture_id,
                              webrtcCaps,
                              cb);
}

int
CamerasChild::StartCapture(CaptureEngine aCapEngine,
                           const int capture_id,
                           webrtc::CaptureCapability& webrtcCaps,
                           webrtc::ExternalRenderer* cb)
{
  LOG((__PRETTY_FUNCTION__));
  AddCallback(aCapEngine, capture_id, cb);
  CaptureCapability capCap(webrtcCaps.width,
                           webrtcCaps.height,
                           webrtcCaps.maxFPS,
                           webrtcCaps.expectedCaptureDelay,
                           webrtcCaps.rawType,
                           webrtcCaps.codecType,
                           webrtcCaps.interlaced);
  nsRefPtr<nsIRunnable> runnable =
    NS_NewRunnableMethodWithArgs<CaptureEngine, const int,
                                 webrtc::CaptureCapability&,
                                 webrtc::ExternalRenderer*>(
      this, &CamerasChild::SendStopCapture, aCapEngine, capture_id, capCap);
  getThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
}

int StopCapture(CaptureEngine aCapEngine, const int capture_id)
{
  Cameras(true)->StopCapture(aCapEngine, capture_id);
}

int
CamerasChild::StopCapture(aCapEngine, const int capture_id)
{
  LOG((__PRETTY_FUNCTION__));
  nsRefPtr<nsIRunnable> runnable =
    NS_NewRunnableMethodWithArgs<CaptureEngine, const int>(
      this, &CamerasChild::SendStopCapture, aCapEngine, capture_id);
  getThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  // XXX: the stopcapture can be delayed infinitely, what if we
  // end up reusing the capture_id?
  RemoveCallback(aCapEngine, capture_id);
}

class ShutdownRunnable : public nsRunnable {
public:
  ShutdownRunnable() {};

  NS_IMETHOD Run() {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::getMutex());
    LOG(("Closing BackgroundChild"));
    ipc::BackgroundChild::CloseForCurrentThread();
    LOG(("Erasing sCameras & thread (runnable)"));
    CamerasSingleton::getChild() = nullptr;
    CamerasSingleton::getThread() = nullptr;
    return NS_OK;
  }
};

void
CamerasChild::XShutdown()
{
  OffTheBooksMutexAutoLock lock(CamerasSingleton::getMutex());
  if (CamerasSingleton::getThread()) {
    LOG(("Thread exists, dispatching"));
    nsRefPtr<ShutdownRunnable> runnable = new ShutdownRunnable();
    CamerasSingleton::getThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  } else {
    LOG(("XShutdown called without camera thread"));
  }
  LOG(("Erasing sCameras & thread (original thread)"));
  CamerasSingleton::getChild() = nullptr;
  CamerasSingleton::getThread() = nullptr;
}

bool
CamerasChild::RecvDeliverFrame(const int& capEngine,
                               const int& capId,
                               mozilla::ipc::Shmem&& shmem,
                               const int& size,
                               const uint32_t& time_stamp,
                               const int64_t& ntp_time,
                               const int64_t& render_time)
{
  //LOG((__PRETTY_FUNCTION__));
  MutexAutoLock lock(mCallbackMutex);
  CaptureEngine capEng = static_cast<CaptureEngine>(capEngine);
  if (Callback(capEng, capId)) {
    unsigned char* image = shmem.get<unsigned char>();
    Callback(capEng, capId)->DeliverFrame(image, size,
                                          time_stamp,
                                          ntp_time, render_time,
                                          nullptr);
  } else {
    LOG(("DeliverFrame called with dead callback"));
  }
  SendReleaseFrame(shmem);
  return true;
}

bool
CamerasChild::RecvFrameSizeChange(const int& capEngine,
                                  const int& capId,
                                  const int& w, const int& h)
{
  LOG((__PRETTY_FUNCTION__));
  MutexAutoLock lock(mCallbackMutex);
  CaptureEngine capEng = static_cast<CaptureEngine>(capEngine);
  if (Callback(capEng, capId)) {
    Callback(capEng, capId)->FrameSizeChange(w, h, 0);
  } else {
    LOG(("Frame size change with dead callback"));
  }
  return true;
}

CamerasChild::CamerasChild()
  : mCallbackMutex("mozilla::cameras::CamerasChild::mCallbackMutex"),
    mReplyMonitor("mozilla::cameras::CamerasChild::mReplyMonitor")
{
  if (!gCamerasChildLog)
    gCamerasChildLog = PR_NewLogModule("CamerasChild");

  LOG(("CamerasChild: %p", this));

  MOZ_COUNT_CTOR(CamerasChild);
}

CamerasChild::~CamerasChild()
{
  LOG(("~CamerasChild: %p", this));

  XShutdown();

  MOZ_COUNT_DTOR(CamerasChild);
}

PCamerasChild* CreateCamerasChild() {
  return new CamerasChild();
}

webrtc::ExternalRenderer* CamerasChild::Callback(CaptureEngine aCapEngine,
                                                 int capture_id)
{
  for (unsigned int i = 0; i < mCallbacks.Length(); i++) {
    CapturerElement ce = mCallbacks[i];
    if (ce.engine == aCapEngine && ce.id == capture_id) {
      return ce.callback;
    }
  }

  return nullptr;
}

}
}
