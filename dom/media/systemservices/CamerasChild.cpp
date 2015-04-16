/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "webrtc/video_engine/include/vie_capture.h"
#undef FF

#include "mozilla/Assertions.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "prlog.h"

#include "CamerasUtils.h"
#include "CamerasChild.h"

PRLogModuleInfo *gCamerasChildLog;

#undef LOG
#undef LOG_ENABLED
#if defined(PR_LOGGING)
#define LOG(args) PR_LOG(gCamerasChildLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gCamerasChildLog, 5)
#else
#define LOG(args)
#define LOG_ENABLED() (false)
#endif

namespace mozilla {
namespace camera {

static PCamerasChild* sCameras;
nsCOMPtr<nsIThread> sCamerasChildThread;

static CamerasChild*
Cameras()
{
  if (!sCameras) {
    // Try to get the PBackground handle
    ipc::PBackgroundChild* existingBackgroundChild =
      ipc::BackgroundChild::GetForCurrentThread();
    // If it's not spun up yet, block until it is, and retry
    if (!existingBackgroundChild) {
      SynchronouslyCreatePBackground();
      existingBackgroundChild =
        ipc::BackgroundChild::GetForCurrentThread();
    }
    // By now PBackground is guaranteed to be up
    MOZ_RELEASE_ASSERT(existingBackgroundChild);
    // Create PCameras by sending a message to the parent
    sCameras = existingBackgroundChild->SendPCamerasConstructor();
    sCamerasChildThread = NS_GetCurrentThread();
  }
  MOZ_ASSERT(sCameras);
  return static_cast<CamerasChild*>(sCameras);
}

int NumberOfCapabilities(CaptureEngine aCapEngine, const char* deviceUniqueIdUTF8)
{
  int numCaps = 0;
  LOG(("NumberOfCapabilities for %s", deviceUniqueIdUTF8));
  nsCString unique_id(deviceUniqueIdUTF8);
  Cameras()->SendNumberOfCapabilities(aCapEngine, unique_id, &numCaps);
  LOG(("Capture capability count: %d", numCaps));
  return numCaps;
}

int GetCaptureCapability(CaptureEngine aCapEngine, const char* unique_idUTF8,
                         const unsigned int capability_number,
                         webrtc::CaptureCapability& capability)
{
  LOG(("GetCaptureCapability: %s %d", unique_idUTF8, capability_number));
  nsCString unique_id(unique_idUTF8);
  CaptureCapability ipcCapability;
  Cameras()->SendGetCaptureCapability(aCapEngine,
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

int NumberOfCaptureDevices(CaptureEngine aCapEngine)
{
  int numCapDevs = 0;
  Cameras()->SendNumberOfCaptureDevices(aCapEngine, &numCapDevs);
  // Note: This is typically the first call, so there's no guarantee
  // gLog is initialized yet before the Cameras() call.
  LOG(("Capture Devices: %d", numCapDevs));
  return numCapDevs;
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
  if (Cameras()->SendGetCaptureDevice(aCapEngine,
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
  if (Cameras()->SendAllocateCaptureDevice(aCapEngine,
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
  if (Cameras()->SendReleaseCaptureDevice(aCapEngine, capture_id)) {
    return 0;
  } else {
    return -1;
  }
}

void
CamerasChild::AddCallback(const CaptureEngine aCapEngine, const int capture_id,
                          webrtc::ExternalRenderer* render)
{
  CapturerElement ce;
  ce.engine = aCapEngine;
  ce.id = capture_id;
  ce.callback = render;
  mCallbacks.AppendElement(ce);
}

void
CamerasChild::RemoveCallback(const CaptureEngine aCapEngine, const int capture_id)
{
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
  LOG((__PRETTY_FUNCTION__));
  Cameras()->AddCallback(aCapEngine, capture_id, cb);
  CaptureCapability capCap(webrtcCaps.width,
                           webrtcCaps.height,
                           webrtcCaps.maxFPS,
                           webrtcCaps.expectedCaptureDelay,
                           webrtcCaps.rawType,
                           webrtcCaps.codecType,
                           webrtcCaps.interlaced);
  if (Cameras()->SendStartCapture(aCapEngine, capture_id, capCap)) {
    return 0;
  } else {
    return -1;
  }
}

int StopCapture(CaptureEngine aCapEngine, const int capture_id)

{
  LOG((__PRETTY_FUNCTION__));
  if (Cameras()->SendStopCapture(aCapEngine, capture_id)) {
    Cameras()->RemoveCallback(aCapEngine, capture_id);
    return 0;
  } else {
    return -1;
  }
}

class ShutdownRunnable : public nsRunnable {
public:
  ShutdownRunnable() {};

  NS_IMETHOD Run() {
    ipc::BackgroundChild::CloseForCurrentThread();
    return NS_OK;
  }
};

void Shutdown()
{
  LOG((__PRETTY_FUNCTION__));
  if (sCamerasChildThread) {
    nsRefPtr<ShutdownRunnable> runnable = new ShutdownRunnable();
    sCamerasChildThread->Dispatch(runnable, NS_DISPATCH_SYNC);
    sCamerasChildThread = nullptr;
  }
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
  LOG((__PRETTY_FUNCTION__));
  CaptureEngine capEng = static_cast<CaptureEngine>(capEngine);
  if (Cameras()->Callback(capEng, capId)) {
    unsigned char* image = shmem.get<unsigned char>();
    Cameras()->Callback(capEng, capId)->DeliverFrame(image, size,
                                                     time_stamp,
                                                     ntp_time, render_time,
                                                     nullptr);
    Cameras()->SendReleaseFrame(shmem);
  } else {
    LOG(("DeliverFrame called with dead callback"));
  }
  return true;
}

bool
CamerasChild::RecvFrameSizeChange(const int& capEngine,
                                  const int& capId,
                                  const int& w, const int& h)
{
  LOG((__PRETTY_FUNCTION__));
  // XXX: Needs a lock
  CaptureEngine capEng = static_cast<CaptureEngine>(capEngine);
  if (Cameras()->Callback(capEng, capId)) {
    Cameras()->Callback(capEng, capId)->FrameSizeChange(w, h, 0);
  } else {
    LOG(("Frame size change with dead callback"));
  }
  return true;
}

CamerasChild::CamerasChild()
{
#if defined(PR_LOGGING)
  if (!gCamerasChildLog)
    gCamerasChildLog = PR_NewLogModule("CamerasChild");
#endif

  LOG(("CamerasChild: %p", this));

  MOZ_COUNT_CTOR(CamerasChild);
}

CamerasChild::~CamerasChild()
{
  LOG(("~CamerasChild: %p", this));

  Shutdown();

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
