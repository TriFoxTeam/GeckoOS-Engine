/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/FlushICache.h"

#ifdef JS_CODEGEN_ARM64
#  include "jit/arm64/vixl/MozCachingDecoder.h"
#  include "jit/arm64/vixl/Simulator-vixl.h"
#endif

#if defined(JS_CODEGEN_ARM) || defined(JS_CODEGEN_ARM64)

#  ifdef __linux__
#    include <linux/version.h>
#    define LINUX_HAS_MEMBARRIER (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 3, 0))
#  else
#    define LINUX_HAS_MEMBARRIER 0
#  endif

#  if LINUX_HAS_MEMBARRIER || defined(__android__)
#    include <string.h>

#    if LINUX_HAS_MEMBARRIER
#      include <linux/membarrier.h>
#      include <sys/syscall.h>
#      include <sys/utsname.h>
#      include <unistd.h>
#    elif defined(__android__)
#      include <sys/syscall.h>
#      include <unistd.h>
#    else
#      error "Missing platform-specific declarations for membarrier syscall!"
#    endif  // __linux__ / ANDROID

static int membarrier(int cmd, int flags) {
  return syscall(__NR_membarrier, cmd, flags);
}

// These definitions come from the Linux kernel source, for kernels before 4.16
// which didn't have access to these membarrier commands.
#    ifndef MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE
#      define MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE (1 << 5)
#    endif

#    ifndef MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE
#      define MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE (1 << 6)
#    endif
#  endif  // LINUX_HAS_MEMBARRIER || defined(__android__)

using namespace js;
using namespace js::jit;

namespace js {
namespace jit {

bool CanFlushExecutionContextForAllThreads() {
#  if (LINUX_HAS_MEMBARRIER || defined(__android__))
  // We only need to decide this once, but we don't know which thread will ask
  // first.  Hence the atomic.
  enum class MemBarrierAvailable : uint32_t { Unset, No, Yes };

  // The only allowable transitions are Unset->No and Unset->Yes.
  static mozilla::Atomic<MemBarrierAvailable> state(MemBarrierAvailable::Unset);

  MemBarrierAvailable localState = state;
  if (MOZ_LIKELY(localState != MemBarrierAvailable::Unset)) {
    return localState == MemBarrierAvailable::Yes;
  }

  // Otherwise we'll have to make enquiries.  This needs to produce the same
  // result if performed more than once.

  // On linux, check the kernel supports membarrier(2).  A prerequisite is that
  // the kernel version is 4.16 or later.
  //
  // Note: this code has been extracted (August 2020) from
  // https://android.googlesource.com/platform/art/+/58520dfba31d6eeef75f5babff15e09aa28e5db8/libartbase/base/membarrier.cc#50
  static constexpr int kRequiredMajor = 4;
  static constexpr int kRequiredMinor = 16;

  struct utsname uts;
  int major, minor;
  bool memBarrierAvailable =
      uname(&uts) == 0 && strcmp(uts.sysname, "Linux") == 0 &&
      sscanf(uts.release, "%d.%d", &major, &minor) == 2 &&
      major >= kRequiredMajor &&
      (major != kRequiredMajor || minor >= kRequiredMinor);

  // Try to run the syscall with the command registering the intent to use the
  // actual membarrier we'll want to carry out later.
  //
  // IMPORTANT: This is needs to succeed, since otherwise running the
  // membarrier later won't actually interrupt the threads in this process.
  // Per `man 2 membarrier`, "RETURN VALUE", we are assured that multiple calls
  // to `membarrier` with the same commands always produce the same results.
  if (memBarrierAvailable &&
      membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, 0) != 0) {
    memBarrierAvailable = false;
  }

  bool ok = state.compareExchange(
      MemBarrierAvailable::Unset,
      memBarrierAvailable ? MemBarrierAvailable::Yes : MemBarrierAvailable::No);
  if (ok) {
    return memBarrierAvailable;
  }

  // We got out-raced.  Return the info that's already there.
  MOZ_ASSERT(state != MemBarrierAvailable::Unset);
  return state == MemBarrierAvailable::Yes;

#  else
  // On other platforms, we assume that the syscall for flushing the icache
  // will flush the execution context for other cores.
  return true;
#  endif
}

void FlushExecutionContextForAllThreads() {
  // Callers must check that this operation is available.
  MOZ_RELEASE_ASSERT(CanFlushExecutionContextForAllThreads());

#  if defined(JS_SIMULATOR_ARM64) && defined(JS_CACHE_SIMULATOR_ARM64)
  // Emulate what the real hardware would do by emitting a membarrier that'll
  // interrupt and flush the execution context of all threads.
  using js::jit::SimulatorProcess;
  js::jit::AutoLockSimulatorCache alsc;
  SimulatorProcess::membarrier();
#  elif (LINUX_HAS_MEMBARRIER || defined(__android__))
  // The caller has checked this can be performed, which will have registered
  // this process to receive the membarrier. See above.
  //
  // membarrier will trigger an inter-processor-interrupt on any active threads
  // of this process. This is an execution context synchronization event
  // equivalent to running an `isb` instruction.
  if (membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0) != 0) {
    // Better safe than sorry.
    MOZ_CRASH("membarrier can't be executed");
  }
#  else
  // On other platforms, we assume that the syscall for flushing the icache
  // will flush the execution context for other cores.
#  endif
}

}  // namespace jit
}  // namespace js

#endif
