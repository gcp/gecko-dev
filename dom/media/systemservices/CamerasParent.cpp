/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "CamerasParent.h"
#include "CamerasUtils.h"
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
  return false;
}

bool
CamerasParent::RecvReleaseCamera(bool* rv)
{
  return false;
}

bool
CamerasParent::RecvNumberOfCaptureDevices(int* numdev)
{
  return false;
}

bool
CamerasParent::RecvNumberOfCapabilities(const nsCString&, int*)
{
  return false;
}

bool
CamerasParent::RecvGetCaptureCapability(const nsCString&, const int&,
                              CaptureCapability*)
{
  return false;
}

bool
CamerasParent::RecvGetCaptureDevice(const int&, nsCString*, nsCString*)
{
  return false;
}

void
CamerasParent::ActorDestroy(ActorDestroyReason aWhy)
{
  // No more IPC from here
}

CamerasParent::CamerasParent()
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
}

PCamerasParent* CreateCamerasParent() {
  return new CamerasParent();
}

}
}
