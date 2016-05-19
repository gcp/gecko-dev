/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXPCOM.h"
#include "nsXULAppAPI.h"
#include "nsAutoPtr.h"

// FIXME/cjones testing
#if !defined(OS_WIN)
#include <unistd.h>
#endif

#ifdef XP_WIN
#include <windows.h>
// we want a wmain entry point
// but we don't want its DLL load protection, because we'll handle it here
#define XRE_DONT_PROTECT_DLL_LOAD
#include "nsWindowsWMain.cpp"
#include "nsSetDllDirectory.h"
#endif

#include "GMPLoader.h"

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
#include "mozilla/sandboxing/SandboxInitialization.h"
#include "mozilla/sandboxing/sandboxLogging.h"
#endif

#if defined(XP_LINUX) && defined(MOZ_GMP_SANDBOX)
#include "mozilla/Sandbox.h"
#include "mozilla/SandboxInfo.h"
#endif

#ifdef MOZ_WIDGET_GONK
# include <sys/time.h>
# include <sys/resource.h> 

# include <binder/ProcessState.h>

# ifdef LOGE_IF
#  undef LOGE_IF
# endif

# include <android/log.h>
# define LOGE_IF(cond, ...) \
     ( (CONDITION(cond)) \
     ? ((void)__android_log_print(ANDROID_LOG_ERROR, \
       "Gecko:MozillaRntimeMain", __VA_ARGS__)) \
     : (void)0 )

# ifdef MOZ_CONTENT_SANDBOX
# include "mozilla/Sandbox.h"
# endif

#endif // MOZ_WIDGET_GONK

#ifdef MOZ_NUWA_PROCESS
#include <binder/ProcessState.h>
#include "ipc/Nuwa.h"
#endif

#if 1
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>

void print_stacktrace()
{
  void *buffer[100];
  int nptrs = backtrace(buffer, 100);
  char **strings = backtrace_symbols(buffer, nptrs);
  if (!strings)
    return;

  for (int i=0; i<nptrs; i++)
    printf("%s\n", strings[i]);

  free(strings);
}

extern "C" MOZ_EXPORT int chmod(const char *pathname, mode_t mode)
{
  static auto real_func = (int (*)(const char*, mode_t))
    dlsym(RTLD_NEXT, "chmod");

  if (!real_func)
    return -1;

  printf("[content] calling chmod()...\n");

  int ret = real_func(pathname, mode);

  printf("[content] chmod('%s', 0x%08x) = %d\n", pathname, mode, ret);

  return ret;
}

extern "C" MOZ_EXPORT int mkdir(const char *pathname, mode_t mode)
{
  static auto real_func = (int (*)(const char*, mode_t))
    dlsym(RTLD_NEXT, "mkdir");

  if (!real_func)
    return -1;

  printf("[content] calling mkdir()...\n");

  int ret = real_func(pathname, mode);

  printf("[content] mkdir('%s', 0x%08x) = %d\n", pathname, mode, ret);

  if (strcmp("/home/user/.config/pulse", pathname))
    print_stacktrace();

  return ret;
}

extern "C" MOZ_EXPORT int rmdir(const char *pathname)
{
  static auto real_func = (int (*)(const char*))
    dlsym(RTLD_NEXT, "rmdir");

  if (!real_func)
    return -1;

  printf("[content] calling rmdir()...\n");

  int ret = real_func(pathname);

  printf("[content] rmdir('%s') = %d\n", pathname, ret);
  return ret;
}

extern "C" MOZ_EXPORT int rename(const char *oldpath, const char *newpath)
{
  static auto real_func = (int (*)(const char*, const char*))
    dlsym(RTLD_NEXT, "rename");

  if (!real_func)
    return -1;

  printf("[content] calling rename()...\n");

  int ret = real_func(oldpath, newpath);

  printf("[content] rename('%s', '%s') = %d\n", oldpath, newpath, ret);
  return ret;
}

extern "C" MOZ_EXPORT int symlink(const char *target, const char *linkpath)
{
  static auto real_func = (int (*)(const char*, const char*))
    dlsym(RTLD_NEXT, "symlink");

  if (!real_func)
    return -1;

  printf("[content] calling symlink()...\n");

  int ret = real_func(target, linkpath);

  printf("[content] symlink('%s', '%s') = %d\n", target, linkpath, ret);
  return ret;
}

extern "C" MOZ_EXPORT int open(const char *pathname, int flags)
{
  static auto real_func = (int (*)(const char*, int))
    dlsym(RTLD_NEXT, "open");

  if (!real_func)
    return -1;

  printf("[content] calling open()...\n");

  int ret = real_func(pathname, flags);

  printf("[content] open('%s', 0x%08x) = %d\n", pathname, flags, ret);
  return ret;
}

extern "C" MOZ_EXPORT int openat(int dirfd, const char *pathname, int flags)
{
  static auto real_func = (int (*)(int, const char*, int))
    dlsym(RTLD_NEXT, "openat");

  if (!real_func)
    return -1;

  printf("[content] calling openat()...\n");

  int ret = real_func(dirfd, pathname, flags);

  printf("[content] openat(0x%08x, '%s', 0x%08x) = %d\n", dirfd, pathname, flags, ret);
  return ret;
}

extern "C" MOZ_EXPORT int access(const char *pathname, int mode)
{
  static auto real_func = (int (*)(const char*, int))
    dlsym(RTLD_NEXT, "access");

  if (!real_func)
    return -1;

  printf("[content] calling access()...\n");

  int ret = real_func(pathname, mode);

  printf("[content] access('%s', 0x%08x) = %d\n", pathname, mode, ret);
  return ret;
}

extern "C" MOZ_EXPORT int faccessat(int dirfd, const char *pathname, int mode, int flags)
{
  static auto real_func = (int (*)(int, const char*, int, int))
    dlsym(RTLD_NEXT, "faccessat");

  if (!real_func)
    return -1;

  printf("[content] calling faccessat()...\n");

  int ret = real_func(dirfd, pathname, mode, flags);

  printf("[content] faccessat(0x%08x, '%s', 0x%08x, 0x%08x) = %d\n", dirfd, pathname, mode, flags, ret);
  return ret;
}

#if 0
extern "C" MOZ_EXPORT int stat(const char *pathname, struct stat *buf)
{
  static auto real_func = (int (*)(const char*, struct stat*))
    dlsym(RTLD_NEXT, "stat");

  if (!real_func)
    return -1;

  int ret = real_func(pathname, buf);

  printf("[content] stat('%s', %p) = %d\n", pathname, buf, ret);
  return ret;
}
#endif

extern "C" MOZ_EXPORT int lstat(const char *pathname, struct stat *buf)
{
  static auto real_func = (int (*)(const char*, struct stat*))
    dlsym(RTLD_NEXT, "lstat");

  if (!real_func)
    return -1;

  printf("[content] calling lstat()...\n");

  int ret = real_func(pathname, buf);

  printf("[content] lstat('%s', %p) = %d\n", pathname, buf, ret);
  return ret;
}

extern "C" MOZ_EXPORT int fstatat(int dirfd, const char *pathname, struct stat *buf, int flags)
{
  static auto real_func = (int (*)(int, const char*, struct stat*, int))
    dlsym(RTLD_NEXT, "fststat");

  if (!real_func)
    return -1;

  printf("[content] calling fststat()...\n");

  int ret = real_func(dirfd, pathname, buf, flags);

  printf("[content] fststat(0x%08x, '%s', %p, 0x%08x) = %d\n", dirfd, pathname, buf, flags, ret);
  return ret;
}

extern "C" MOZ_EXPORT int statfs(const char *path, struct statfs *buf)
{
  static auto real_func = (int (*)(const char*, struct statfs*))
    dlsym(RTLD_NEXT, "statfs");

  if (!real_func)
    return -1;

  printf("[content] calling statfs()...\n");

  int ret = real_func(path, buf);

  printf("[content] statfs('%s', %p) = %d\n", path, buf, ret);
  return ret;
}

extern "C" MOZ_EXPORT int inotify_add_watch(int fd, const char *pathname, uint32_t mask)
{
  static auto real_func = (int (*)(int, const char*, uint32_t))
    dlsym(RTLD_NEXT, "inotify_add_watch");

  if (!real_func)
    return -1;

  print_stacktrace();
  int ret = real_func(fd, pathname, mask);

  printf("[content] inotify_add_watch(0x%08x, '%s', 0x%08x) = %d\n", fd, pathname, mask, ret);
  return ret;
}

#endif

#ifdef MOZ_WIDGET_GONK
static void
InitializeBinder(void *aDummy) {
    // Change thread priority to 0 only during calling ProcessState::self().
    // The priority is registered to binder driver and used for default Binder
    // Thread's priority. 
    // To change the process's priority to small value need's root permission.
    int curPrio = getpriority(PRIO_PROCESS, 0);
    int err = setpriority(PRIO_PROCESS, 0, 0);
    MOZ_ASSERT(!err);
    LOGE_IF(err, "setpriority failed. Current process needs root permission.");
    android::ProcessState::self()->startThreadPool();
    setpriority(PRIO_PROCESS, 0, curPrio);
}
#endif

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
static bool gIsSandboxEnabled = false;

class WinSandboxStarter : public mozilla::gmp::SandboxStarter {
public:
    virtual bool Start(const char *aLibPath) override {
        if (gIsSandboxEnabled) {
            mozilla::sandboxing::LowerSandbox();
        }
        return true;
    }
};
#endif

#if defined(XP_LINUX) && defined(MOZ_GMP_SANDBOX)
class LinuxSandboxStarter : public mozilla::gmp::SandboxStarter {
    LinuxSandboxStarter() { }
public:
    static SandboxStarter* Make() {
        if (mozilla::SandboxInfo::Get().CanSandboxMedia()) {
            return new LinuxSandboxStarter();
        } else {
            // Sandboxing isn't possible, but the parent has already
            // checked that this plugin doesn't require it.  (Bug 1074561)
            return nullptr;
        }
    }
    virtual bool Start(const char *aLibPath) override {
        mozilla::SetMediaPluginSandbox(aLibPath);
        return true;
    }
};
#endif

#if defined(XP_MACOSX) && defined(MOZ_GMP_SANDBOX)
class MacSandboxStarter : public mozilla::gmp::SandboxStarter {
public:
    virtual bool Start(const char *aLibPath) override {
      std::string err;
      bool rv = mozilla::StartMacSandbox(mInfo, err);
      if (!rv) {
        fprintf(stderr, "sandbox_init() failed! Error \"%s\"\n", err.c_str());
      }
      return rv;
    }
    virtual void SetSandboxInfo(MacSandboxInfo* aSandboxInfo) override {
      mInfo = *aSandboxInfo;
    }
private:
  MacSandboxInfo mInfo;
};
#endif

mozilla::gmp::SandboxStarter*
MakeSandboxStarter()
{
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
    return new WinSandboxStarter();
#elif defined(XP_LINUX) && defined(MOZ_GMP_SANDBOX)
    return LinuxSandboxStarter::Make();
#elif defined(XP_MACOSX) && defined(MOZ_GMP_SANDBOX)
    return new MacSandboxStarter();
#else
    return nullptr;
#endif
}

int
content_process_main(int argc, char* argv[])
{
    // Check for the absolute minimum number of args we need to move
    // forward here. We expect the last arg to be the child process type.
    if (argc < 1) {
      return 3;
    }

    bool isNuwa = false;
    for (int i = 1; i < argc; i++) {
        isNuwa |= strcmp(argv[i], "-nuwa") == 0;
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
        gIsSandboxEnabled |= strcmp(argv[i], "-sandbox") == 0;
#endif
    }

    XREChildData childData;

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
    if (gIsSandboxEnabled) {
        childData.sandboxTargetServices =
            mozilla::sandboxing::GetInitializedTargetServices();
        if (!childData.sandboxTargetServices) {
            return 1;
        }

        childData.ProvideLogFunction = mozilla::sandboxing::ProvideLogFunction;
    }
#endif

    XRE_SetProcessType(argv[--argc]);

#ifdef MOZ_NUWA_PROCESS
    if (isNuwa) {
        PrepareNuwaProcess();
    }
#endif

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
    // This has to happen while we're still single-threaded, and on
    // B2G that means before the Android Binder library is
    // initialized.  Additional special handling is needed for Nuwa:
    // the Nuwa process itself needs to be unsandboxed, and the same
    // single-threadedness condition applies to its children; see also
    // AfterNuwaFork().
    mozilla::SandboxEarlyInit(XRE_GetProcessType(), isNuwa);
#endif

#ifdef MOZ_WIDGET_GONK
    // This creates a ThreadPool for binder ipc. A ThreadPool is necessary to
    // receive binder calls, though not necessary to send binder calls.
    // ProcessState::Self() also needs to be called once on the main thread to
    // register the main thread with the binder driver.

#ifdef MOZ_NUWA_PROCESS
    if (!isNuwa) {
        InitializeBinder(nullptr);
    } else {
        NuwaAddFinalConstructor(&InitializeBinder, nullptr);
    }
#else
    InitializeBinder(nullptr);
#endif
#endif

#ifdef XP_WIN
    // For plugins, this is done in PluginProcessChild::Init, as we need to
    // avoid it for unsupported plugins.  See PluginProcessChild::Init for
    // the details.
    if (XRE_GetProcessType() != GeckoProcessType_Plugin) {
        mozilla::SanitizeEnvironmentVariables();
        SetDllDirectory(L"");
    }
#endif
#if !defined(MOZ_WIDGET_ANDROID) && !defined(MOZ_WIDGET_GONK)
    // On desktop, the GMPLoader lives in plugin-container, so that its
    // code can be covered by an EME/GMP vendor's voucher.
    nsAutoPtr<mozilla::gmp::SandboxStarter> starter(MakeSandboxStarter());
    if (XRE_GetProcessType() == GeckoProcessType_GMPlugin) {
        childData.gmpLoader = mozilla::gmp::CreateGMPLoader(starter);
    }
#endif
    nsresult rv = XRE_InitChildProcess(argc, argv, &childData);
    NS_ENSURE_SUCCESS(rv, 1);

    return 0;
}
