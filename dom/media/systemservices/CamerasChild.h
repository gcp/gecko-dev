/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CamerasChild_h
#define mozilla_CamerasChild_h

#include "mozilla/dom/ContentChild.h"
#include "mozilla/camera/PCamerasChild.h"
#include "mozilla/camera/PCamerasParent.h"

namespace webrtc {
  struct CaptureCapability;
}

namespace mozilla {
namespace camera {

int NumberOfCapabilities(const char* deviceUniqueIdUTF8);
int GetCaptureCapability(const char* unique_idUTF8,
                         const unsigned int capability_number,
                         webrtc::CaptureCapability& capability);
int NumberOfCaptureDevices();
int GetCaptureDevice(unsigned int list_number, char* device_nameUTF8,
                     const unsigned int device_nameUTF8Length,
                     char* unique_idUTF8,
                     const unsigned int unique_idUTF8Length);
int AllocateCaptureDevice(const char* unique_idUTF8,
                          const unsigned int unique_idUTF8Length,
                          int& capture_id);
int ReleaseCaptureDevice(const int capture_id);


class CamerasChild :
  public PCamerasChild
{
public:
  virtual bool RecvDeliverFrame() MOZ_OVERRIDE;
  virtual bool RecvCameraList(nsTArray<Camera>&& args) MOZ_OVERRIDE;

  CamerasChild();
  virtual ~CamerasChild();
};

PCamerasChild* CreateCamerasChild();

} // namespace camera
} // namespace mozilla

#endif  // mozilla_CamerasChild_h
