/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <dlfcn.h>
#include <signal.h>
#include <errno.h>

#include "mozilla/Types.h"

// This file defines a hook for sigprocmask() and pthread_sigmask().
// Bug 1176099: some threads block SIGSYS signal which breaks our seccomp-bpf
// sandbox. To avoid this, we intercept the call and remove SIGSYS.
//
// ENOSYS indicates an error within the hook function itself.
static int HandleSigset(int aHow, const sigset_t* aSet, sigset_t* aNewSet)
{
  *aNewSet = *aSet;
  if (aHow == SIG_UNBLOCK || !sigismember(aNewSet, SIGSYS))
    return 0;

  return sigdelset(aNewSet, SIGSYS);
}

extern "C" MOZ_EXPORT int
sigprocmask(int how, const sigset_t* set, sigset_t* oldset)
{
  static auto gRealSigprocmask = (int (*)(int, const sigset_t*, sigset_t*))
    dlsym(RTLD_NEXT, "sigprocmask");

  sigset_t newSet;
  if (!gRealSigprocmask || HandleSigset(how, set, &newSet) < 0) {
    errno = ENOSYS;
    return -1;
  }

  return gRealSigprocmask(how, &newSet, oldset);
}

extern "C" MOZ_EXPORT int
pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset)
{
  static auto gRealPthreadSigmask = (int (*)(int, const sigset_t*, sigset_t*))
    dlsym(RTLD_NEXT, "pthread_sigmask");

  sigset_t newSet;
  if (!gRealPthreadSigmask || HandleSigset(how, set, &newSet) < 0)
    return ENOSYS;

  return gRealPthreadSigmask(how, &newSet, oldset);
}
