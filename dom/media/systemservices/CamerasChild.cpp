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
#include "mozilla/WeakPtr.h"
#include "MediaUtils.h"
#include "nsThreadUtils.h"
#include "mozilla/Logging.h"

#undef LOG
#undef LOG_ENABLED
PRLogModuleInfo *gCamerasChildLog;
#define LOG(args) MOZ_LOG(gCamerasChildLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() MOZ_LOG_TEST(gCamerasChildLog, mozilla::LogLevel::Debug)

namespace mozilla {
namespace camera {

// We emulate the sync webrtc.org API with the help of singleton
// CamerasSingleton, which manages a pointer to an IPC object, a thread
// where IPC operations should run on, and a mutex.
// The static function Cameras() will use that Singleton to set up,
// if needed, both the thread and the associated IPC objects and return
// a pointer to the IPC object. Users can then do IPC calls on that object
// after dispatching them to aforementioned thread.

class CamerasSingleton {
public:
  CamerasSingleton()
    : mCamerasMutex("CamerasSingleton::mCamerasMutex"),
      mCameras(nullptr),
      mCamerasChildThread(nullptr) {
    if (!gCamerasChildLog) {
      gCamerasChildLog = PR_NewLogModule("CamerasChild");
    }
    LOG(("CamerasSingleton: %p", this));
  }

  ~CamerasSingleton() {
    LOG(("~CamerasSingleton: %p", this));
  }

  static CamerasSingleton& GetInstance() {
    static CamerasSingleton instance;
    return instance;
  }

  static OffTheBooksMutex& GetMutex() {
    return GetInstance().mCamerasMutex;
  }

  static CamerasChild*& GetChild() {
    GetInstance().GetMutex().AssertCurrentThreadOwns();
    return GetInstance().mCameras;
  }

  static nsCOMPtr<nsIThread>& GetThread() {
    GetInstance().GetMutex().AssertCurrentThreadOwns();
    return GetInstance().mCamerasChildThread;
  }

private:
  // Reinitializing CamerasChild will change the pointers below.
  // We don't want this to happen in the middle of preparing IPC.
  // We will be alive on destruction, so this needs to be off the books.
  mozilla::OffTheBooksMutex mCamerasMutex;

  CamerasChild* mCameras;
  nsCOMPtr<nsIThread> mCamerasChildThread;
};

class InitializeIPCThread : public nsRunnable
{
public:
  explicit InitializeIPCThread()
    : mCamerasChild(nullptr) {}

  NS_IMETHOD Run() override {
    // Try to get the PBackground handle
    ipc::PBackgroundChild* existingBackgroundChild =
      ipc::BackgroundChild::GetForCurrentThread();
    // If it's not spun up yet, block until it is, and retry
    if (!existingBackgroundChild) {
      LOG(("No existingBackgroundChild"));
      SynchronouslyCreatePBackground();
      existingBackgroundChild =
        ipc::BackgroundChild::GetForCurrentThread();
      LOG(("BackgroundChild: %p", existingBackgroundChild));
    }
    // By now PBackground is guaranteed to be up
    MOZ_RELEASE_ASSERT(existingBackgroundChild);

    // Create CamerasChild
    mCamerasChild =
      static_cast<mozilla::camera::CamerasChild*>(existingBackgroundChild->SendPCamerasConstructor());

    return NS_OK;
  }

  CamerasChild* GetCamerasChild() {
    MOZ_ASSERT(mCamerasChild);
    return mCamerasChild;
  }

private:
  CamerasChild* mCamerasChild;
};

static CamerasChild* Cameras(bool trace) {
  if (!gCamerasChildLog)
    gCamerasChildLog = PR_NewLogModule("CamerasChild");

  OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
  if (!CamerasSingleton::GetChild()) {
    LOG(("No sCameras, setting up"));
    MOZ_ASSERT(!CamerasSingleton::GetThread());
    LOG(("Spinning up IPC Thread"));
    nsresult rv = NS_NewNamedThread("Cameras IPC",
      getter_AddRefs(CamerasSingleton::GetThread()));
    if (NS_FAILED(rv)) {
      LOG(("Error launching IPC Thread"));
      return nullptr;
    }

    // Block until:
    // 1) Creation of PBackground on the thread created earlier
    // 2) Creation of PCameras by sending a message to the parent on that thread
    nsRefPtr<InitializeIPCThread> runnable = new InitializeIPCThread();
    nsRefPtr<SyncRunnable> sr = new SyncRunnable(runnable);
    sr->DispatchToThread(CamerasSingleton::GetThread());
    CamerasSingleton::GetChild() = runnable->GetCamerasChild();
  }
  if (trace) {
    CamerasChild* tmp = CamerasSingleton::GetChild();
    LOG(("Returning sCameras: %p", tmp));
  }
  MOZ_ASSERT(CamerasSingleton::GetChild());
  return CamerasSingleton::GetChild();
}

bool
CamerasChild::RecvReplyFailure(void)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplySuccess = false;
  monitor.Notify();
  return true;
}

bool
CamerasChild::RecvReplySuccess(void)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplySuccess = true;
  monitor.Notify();
  return true;
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
  mReplySuccess = true;
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
    media::NewRunnableFrom([this, aCapEngine, unique_id]() -> nsresult {
      if (this->SendNumberOfCapabilities(aCapEngine, unique_id)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  // We can't see if the send worked, so we need to be able to bail
  // out on shutdown (when it failed and we won't get a reply).
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  if (mReplySuccess) {
    LOG(("Capture capability count: %d", mReplyInteger));
    return mReplyInteger;
  } else {
    LOG(("Get capture capability count failed"));
    return 0;
  }
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
    media::NewRunnableFrom([this, aCapEngine]() -> nsresult {
      if (this->SendNumberOfCaptureDevices(aCapEngine)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  if (mReplySuccess) {
    LOG(("Capture Devices: %d", mReplyInteger));
    return mReplyInteger;
  } else {
    LOG(("Get NumberOfCaptureDevices failed"));
    return 0;
  }
}

bool
CamerasChild::RecvReplyNumberOfCaptureDevices(const int& numdev)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplySuccess = true;
  mReplyInteger = numdev;
  monitor.Notify();
  return true;
}

int GetCaptureCapability(CaptureEngine aCapEngine, const char* unique_idUTF8,
                         const unsigned int capability_number,
                         webrtc::CaptureCapability& capability)
{
  return Cameras(true)->GetCaptureCapability(aCapEngine,
                                             unique_idUTF8,
                                             capability_number,
                                             capability);
}

int
CamerasChild::GetCaptureCapability(CaptureEngine aCapEngine,
                                   const char* unique_idUTF8,
                                   const unsigned int capability_number,
                                   webrtc::CaptureCapability& capability)
{
  LOG(("GetCaptureCapability: %s %d", unique_idUTF8, capability_number));
  MonitorAutoLock monitor(mReplyMonitor);
  nsCString unique_id(unique_idUTF8);
  nsRefPtr<nsIRunnable> runnable =
    media::NewRunnableFrom([this, aCapEngine, unique_id, capability_number]() -> nsresult {
      if (this->SendGetCaptureCapability(aCapEngine, unique_id, capability_number)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  if (mReplySuccess) {
    capability = mReplyCapability;
    return 0;
  } else {
    return -1;
  }
}

bool
CamerasChild::RecvReplyGetCaptureCapability(const CaptureCapability& ipcCapability)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplySuccess = true;
  mReplyCapability.width = ipcCapability.width();
  mReplyCapability.height = ipcCapability.height();
  mReplyCapability.maxFPS = ipcCapability.maxFPS();
  mReplyCapability.expectedCaptureDelay = ipcCapability.expectedCaptureDelay();
  mReplyCapability.rawType = static_cast<webrtc::RawVideoType>(ipcCapability.rawType());
  mReplyCapability.codecType = static_cast<webrtc::VideoCodecType>(ipcCapability.codecType());
  mReplyCapability.interlaced = ipcCapability.interlaced();
  monitor.Notify();
  return true;
}


int GetCaptureDevice(CaptureEngine aCapEngine,
                     unsigned int list_number, char* device_nameUTF8,
                     const unsigned int device_nameUTF8Length,
                     char* unique_idUTF8,
                     const unsigned int unique_idUTF8Length)
{
  return Cameras(true)->GetCaptureDevice(aCapEngine,
                                         list_number,
                                         device_nameUTF8,
                                         device_nameUTF8Length,
                                         unique_idUTF8,
                                         unique_idUTF8Length);
}

int
CamerasChild::GetCaptureDevice(CaptureEngine aCapEngine,
                               unsigned int list_number, char* device_nameUTF8,
                               const unsigned int device_nameUTF8Length,
                               char* unique_idUTF8,
                               const unsigned int unique_idUTF8Length)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  nsRefPtr<nsIRunnable> runnable =
    media::NewRunnableFrom([this, aCapEngine, list_number]() -> nsresult {
      if (this->SendGetCaptureDevice(aCapEngine, list_number)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  if (mReplySuccess) {
    base::strlcpy(device_nameUTF8, mReplyDeviceName.get(), device_nameUTF8Length);
    base::strlcpy(unique_idUTF8, mReplyDeviceID.get(), unique_idUTF8Length);
    LOG(("Got %s name %s id", device_nameUTF8, unique_idUTF8));
    return 0;
  } else {
    LOG(("GetCaptureDevice failed"));
    return -1;
  }
}

bool
CamerasChild::RecvReplyGetCaptureDevice(const nsCString& device_name,
                                        const nsCString& device_id)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplySuccess = true;
  mReplyDeviceName = device_name;
  mReplyDeviceID = device_id;
  monitor.Notify();
  return true;
}

int AllocateCaptureDevice(CaptureEngine aCapEngine,
                          const char* unique_idUTF8,
                          const unsigned int unique_idUTF8Length,
                          int& capture_id)
{
  return Cameras(true)->AllocateCaptureDevice(aCapEngine,
                                              unique_idUTF8,
                                              unique_idUTF8Length,
                                              capture_id);
}

int
CamerasChild::AllocateCaptureDevice(CaptureEngine aCapEngine,
                                    const char* unique_idUTF8,
                                    const unsigned int unique_idUTF8Length,
                                    int& capture_id)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  nsCString unique_id(unique_idUTF8);
  nsRefPtr<nsIRunnable> runnable =
    media::NewRunnableFrom([this, aCapEngine, unique_id]() -> nsresult {
      if (this->SendAllocateCaptureDevice(aCapEngine, unique_id)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  if (mReplySuccess) {
    LOG(("Capture Device allocated: %d", mReplyInteger));
    capture_id = mReplyInteger;
    return 0;
  } else {
    LOG(("AllocateCaptureDevice failed"));
    return -1;
  }
}


bool
CamerasChild::RecvReplyAllocateCaptureDevice(const int& numdev)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  mReplySuccess = true;
  mReplyInteger = numdev;
  monitor.Notify();
  return true;
}

int ReleaseCaptureDevice(CaptureEngine aCapEngine, const int capture_id)
{
  return Cameras(true)->ReleaseCaptureDevice(aCapEngine, capture_id);
}

int
CamerasChild::ReleaseCaptureDevice(CaptureEngine aCapEngine,
                                   const int capture_id)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  nsRefPtr<nsIRunnable> runnable =
    media::NewRunnableFrom([this, aCapEngine, capture_id]() -> nsresult {
      if (this->SendReleaseCaptureDevice(aCapEngine, capture_id)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  if (mReplySuccess) {
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
  return Cameras(true)->StartCapture(aCapEngine,
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
  MonitorAutoLock monitor(mReplyMonitor);
  AddCallback(aCapEngine, capture_id, cb);
  CaptureCapability capCap(webrtcCaps.width,
                           webrtcCaps.height,
                           webrtcCaps.maxFPS,
                           webrtcCaps.expectedCaptureDelay,
                           webrtcCaps.rawType,
                           webrtcCaps.codecType,
                           webrtcCaps.interlaced);
  nsRefPtr<nsIRunnable> runnable =
    media::NewRunnableFrom([this, aCapEngine, capture_id, capCap]() -> nsresult {
      if (this->SendStartCapture(aCapEngine, capture_id, capCap)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  if (mReplySuccess) {
    return 0;
  } else {
    return -1;
  }
}

int StopCapture(CaptureEngine aCapEngine, const int capture_id)
{
  return Cameras(true)->StopCapture(aCapEngine, capture_id);
}

int
CamerasChild::StopCapture(CaptureEngine aCapEngine, const int capture_id)
{
  LOG((__PRETTY_FUNCTION__));
  MonitorAutoLock monitor(mReplyMonitor);
  nsRefPtr<nsIRunnable> runnable =
    media::NewRunnableFrom([this, aCapEngine, capture_id]() -> nsresult {
      if (this->SendStopCapture(aCapEngine, capture_id)) {
        return NS_OK;
      }
      return NS_ERROR_FAILURE;
    });
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }
  if (!mIPCIsAlive) {
    return -1;
  }
  monitor.Wait();
  RemoveCallback(aCapEngine, capture_id);
  if (mReplySuccess) {
    return 0;
  } else {
    return -1;
  }
}

void
Shutdown(void)
{
  {
    OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
    if (!CamerasSingleton::GetChild()) {
      // We don't want to cause everything to get fired up if we're
      // really already shut down.
      LOG(("Shutdown when already shut down"));
      return;
    }
  }
  Cameras(true)->Shutdown();
}

class ShutdownRunnable : public nsRunnable {
public:
  ShutdownRunnable(nsRefPtr<nsIRunnable> aReplyEvent,
                   nsRefPtr<nsIThread> aReplyThread)
    : mReplyEvent(aReplyEvent), mReplyThread(aReplyThread) {};

  NS_IMETHOD Run() override {
    LOG(("Closing BackgroundChild"));
    ipc::BackgroundChild::CloseForCurrentThread();

    LOG(("PBackground thread exists, shutting down thread"));
    mReplyThread->Dispatch(mReplyEvent, NS_DISPATCH_NORMAL);

    return NS_OK;
  }

private:
  nsRefPtr<nsIRunnable> mReplyEvent;
  nsRefPtr<nsIThread> mReplyThread;
};

void
CamerasChild::Shutdown()
{
  {
    MonitorAutoLock monitor(mReplyMonitor);
    mIPCIsAlive = false;
    monitor.NotifyAll();
  }

  OffTheBooksMutexAutoLock lock(CamerasSingleton::GetMutex());
  if (CamerasSingleton::GetThread()) {
    LOG(("PBackground thread exists, dispatching close"));
    // Dispatch closing the IPC thread back to us when the
    // BackgroundChild is closed.
    nsRefPtr<nsIRunnable> event =
      new ThreadDestructor(CamerasSingleton::GetThread());
    nsRefPtr<ShutdownRunnable> runnable =
      new ShutdownRunnable(event, NS_GetCurrentThread());
    CamerasSingleton::GetThread()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  } else {
    LOG(("Shutdown called without PBackground thread"));
  }
  LOG(("Erasing sCameras & thread refs (original thread)"));
  CamerasSingleton::GetChild() = nullptr;
  CamerasSingleton::GetThread() = nullptr;
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

void
CamerasChild::ActorDestroy(ActorDestroyReason aWhy)
{
  MonitorAutoLock monitor(mReplyMonitor);
  mIPCIsAlive = false;
  // Hopefully prevent us from getting stuck
  // on replies that'll never come.
  monitor.NotifyAll();
}

CamerasChild::CamerasChild()
  : mCallbackMutex("mozilla::cameras::CamerasChild::mCallbackMutex"),
    mIPCIsAlive(true),
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

  Shutdown();

  MOZ_COUNT_DTOR(CamerasChild);
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
