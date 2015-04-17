/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CamerasChild_h
#define mozilla_CamerasChild_h

#include "mozilla/Pair.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/camera/PCamerasChild.h"
#include "mozilla/camera/PCamerasParent.h"
#include "mozilla/Mutex.h"

// conflicts with #include of scoped_ptr.h
#undef FF
#include "webrtc/common.h"
// Video Engine
#include "webrtc/video_engine/include/vie_base.h"
#include "webrtc/video_engine/include/vie_capture.h"
#include "webrtc/video_engine/include/vie_render.h"

namespace mozilla {
namespace camera {

enum CaptureEngine : int {
  InvalidEngine,
  ScreenEngine,
  BrowserEngine,
  WinEngine,
  AppEngine,
  CameraEngine
};

struct CapturerElement {
  CaptureEngine engine;
  int id;
  webrtc::ExternalRenderer* callback;
};

int NumberOfCapabilities(CaptureEngine aCapEngine,
                         const char* deviceUniqueIdUTF8);
int GetCaptureCapability(CaptureEngine aCapEngine,
                         const char* unique_idUTF8,
                         const unsigned int capability_number,
                         webrtc::CaptureCapability& capability);
int NumberOfCaptureDevices(CaptureEngine aCapEngine);
int GetCaptureDevice(CaptureEngine aCapEngine,
                     unsigned int list_number, char* device_nameUTF8,
                     const unsigned int device_nameUTF8Length,
                     char* unique_idUTF8,
                     const unsigned int unique_idUTF8Length);
int AllocateCaptureDevice(CaptureEngine aCapEngine,
                          const char* unique_idUTF8,
                          const unsigned int unique_idUTF8Length,
                          int& capture_id);
int ReleaseCaptureDevice(CaptureEngine aCapEngine,
                         const int capture_id);
int StartCapture(CaptureEngine aCapEngine,
                 const int capture_id, webrtc::CaptureCapability& capability,
                 webrtc::ExternalRenderer* func);
int StopCapture(CaptureEngine aCapEngine, const int capture_id);
void Shutdown();

class CamerasChild :
  public PCamerasChild
{
public:
  virtual bool RecvDeliverFrame(const int&, const int&, mozilla::ipc::Shmem&&,
                                const int&, const uint32_t&, const int64_t&,
                                const int64_t&) override;
  virtual bool RecvFrameSizeChange(const int&, const int&,
                                   const int& w, const int& h) override;

  webrtc::ExternalRenderer* Callback(CaptureEngine aCapEngine, int capture_id);
  void AddCallback(const CaptureEngine aCapEngine, const int capture_id,
                   webrtc::ExternalRenderer* render);
  void RemoveCallback(const CaptureEngine aCapEngine, const int capture_id);

  CamerasChild();
  virtual ~CamerasChild();

private:
  nsTArray<CapturerElement> mCallbacks;
  // Protects the callback arrays
  Mutex mMutex;
};

PCamerasChild* CreateCamerasChild();

} // namespace camera
} // namespace mozilla

#endif  // mozilla_CamerasChild_h
