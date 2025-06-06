/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VPX_VPX_PORTS_VPX_TIMER_H_
#define VPX_VPX_PORTS_VPX_TIMER_H_

#include "./vpx_config.h"

#include "vpx/vpx_integer.h"

#if CONFIG_OS_SUPPORT

#if defined(_WIN32)
/*
 * Win32 specific includes
 */
#undef NOMINMAX
#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
/*
 * POSIX specific includes
 */
#include <time.h>

/* timersub is not provided by msys at this time. */
#ifndef timersub_ns
#define timersub_ns(a, b, result)                    \
  do {                                               \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
    if ((result)->tv_nsec < 0) {                     \
      --(result)->tv_sec;                            \
      (result)->tv_nsec += 1000000000;               \
    }                                                \
  } while (0)
#endif
#endif

struct vpx_usec_timer {
#if defined(_WIN32)
  LARGE_INTEGER begin, end;
#else
  struct timespec begin, end;
#endif
};

static INLINE void vpx_usec_timer_start(struct vpx_usec_timer *t) {
#if defined(_WIN32)
  QueryPerformanceCounter(&t->begin);
#elif defined(CLOCK_MONOTONIC_RAW)
  clock_gettime(CLOCK_MONOTONIC_RAW, &t->begin);
#else
  clock_gettime(CLOCK_MONOTONIC, &t->begin);
#endif
}

static INLINE void vpx_usec_timer_mark(struct vpx_usec_timer *t) {
#if defined(_WIN32)
  QueryPerformanceCounter(&t->end);
#elif defined(CLOCK_MONOTONIC_RAW)
  clock_gettime(CLOCK_MONOTONIC_RAW, &t->end);
#else
  clock_gettime(CLOCK_MONOTONIC, &t->end);
#endif
}

static INLINE int64_t vpx_usec_timer_elapsed(struct vpx_usec_timer *t) {
#if defined(_WIN32)
  LARGE_INTEGER freq, diff;

  diff.QuadPart = t->end.QuadPart - t->begin.QuadPart;

  QueryPerformanceFrequency(&freq);
  return diff.QuadPart * 1000000 / freq.QuadPart;
#else
  struct timespec diff;

  timersub_ns(&t->end, &t->begin, &diff);
  return (int64_t)diff.tv_sec * 1000000 + diff.tv_nsec / 1000;
#endif
}

#else /* CONFIG_OS_SUPPORT = 0*/

/* Empty timer functions if CONFIG_OS_SUPPORT = 0 */
#ifndef timersub_ns
#define timersub_ns(a, b, result)
#endif

struct vpx_usec_timer {
  void *dummy;
};

static INLINE void vpx_usec_timer_start(struct vpx_usec_timer *t) {}

static INLINE void vpx_usec_timer_mark(struct vpx_usec_timer *t) {}

static INLINE int vpx_usec_timer_elapsed(struct vpx_usec_timer *t) { return 0; }

#endif /* CONFIG_OS_SUPPORT */

#endif  // VPX_VPX_PORTS_VPX_TIMER_H_
