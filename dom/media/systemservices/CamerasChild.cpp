/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "CamerasUtils.h"
#include "CamerasChild.h"

#include "prlog.h"

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

static PCamerasChild*
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
  }
  MOZ_ASSERT(sCameras);
  return sCameras;
}

void GetCameraList(void)
{
  Cameras()->SendEnumerateCameras();
}

bool
CamerasChild::RecvDeliverFrame()
{
  return false;
}

bool
CamerasChild::RecvCameraList(const nsTArray<Camera>& args)
{
  LOG(("RecvCameraList"));
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
  LOG(("CamerasChild: %p", this));
  MOZ_COUNT_DTOR(CamerasChild);
}

PCamerasChild* CreateCamerasChild() {
  return new CamerasChild();
}

}
}
