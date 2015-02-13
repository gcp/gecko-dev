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

namespace mozilla {
namespace camera {

void GetCameraList(void);

class CamerasChild :
  public PCamerasChild
{
public:
  virtual bool RecvDeliverFrame() MOZ_OVERRIDE;
  virtual bool RecvCameraList(const nsTArray<Camera>& args) MOZ_OVERRIDE;

  CamerasChild();
  virtual ~CamerasChild();
};

PCamerasChild* CreateCamerasChild();

} // namespace camera
} // namespace mozilla

#endif  // mozilla_CamerasChild_h
