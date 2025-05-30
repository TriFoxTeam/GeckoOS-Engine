/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This header contains all definitions related to profiler labels.
// It is safe to include unconditionally, and only defines empty macros if
// MOZ_GECKO_PROFILER is not set.

#ifndef ProfilerLabels_h
#define ProfilerLabels_h

#include "mozilla/ProfilerState.h"
#include "mozilla/ProfilerThreadState.h"

#include "js/Debug.h"
#include "js/ProfilingCategory.h"
#include "js/ProfilingStack.h"
#include "js/RootingAPI.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/BaseProfilerRAIIMacro.h"
#include "mozilla/Maybe.h"
#include "mozilla/ProfilerThreadRegistration.h"
#include "mozilla/ThreadLocal.h"
#include "nsString.h"

#include <stdint.h>

struct JSContext;

// Insert an RAII object in this scope to enter a label stack frame. Any
// samples collected in this scope will contain this label in their stack.
// The label argument must be a static C string. It is usually of the
// form "ClassName::FunctionName". (Ideally we'd use the compiler to provide
// that for us, but __func__ gives us the function name without the class
// name.) If the label applies to only part of a function, you can qualify it
// like this: "ClassName::FunctionName:PartName".
//
// Use AUTO_PROFILER_LABEL_DYNAMIC_* if you want to add additional / dynamic
// information to the label stack frame, and AUTO_PROFILER_LABEL_HOT if you're
// instrumenting functions for which overhead on the order of nanoseconds is
// noticeable.
#define AUTO_PROFILER_LABEL(label, categoryPair) \
  mozilla::AutoProfilerLabel PROFILER_RAII(      \
      label, nullptr, JS::ProfilingCategoryPair::categoryPair)

// Like AUTO_PROFILER_LABEL, but for super-hot code where overhead must be
// kept to the absolute minimum. This variant doesn't push the label if the
// profiler isn't running.
// Don't use this for long-running functions: If the profiler is started in
// the middle of the function, this label won't be on the stack until the
// function is entered the next time. As a result, category information for
// samples at the start of the profile can be misleading.
// For short-running functions, that's often an acceptable trade-off.
#define AUTO_PROFILER_LABEL_HOT(label, categoryPair) \
  mozilla::AutoProfilerLabelHot PROFILER_RAII(       \
      label, nullptr, JS::ProfilingCategoryPair::categoryPair)

// Similar to AUTO_PROFILER_LABEL, but that adds the RELEVANT_FOR_JS flag.
#define AUTO_PROFILER_LABEL_RELEVANT_FOR_JS(label, categoryPair) \
  mozilla::AutoProfilerLabel PROFILER_RAII(                      \
      label, nullptr, JS::ProfilingCategoryPair::categoryPair,   \
      uint32_t(js::ProfilingStackFrame::Flags::RELEVANT_FOR_JS))

// Similar to AUTO_PROFILER_LABEL, but with only one argument: the category
// pair. The label string is taken from the category pair. This is convenient
// for labels like AUTO_PROFILER_LABEL_CATEGORY_PAIR(GRAPHICS_LayerBuilding)
// which would otherwise just repeat the string.
#define AUTO_PROFILER_LABEL_CATEGORY_PAIR(categoryPair)     \
  mozilla::AutoProfilerLabel PROFILER_RAII(                 \
      "", nullptr, JS::ProfilingCategoryPair::categoryPair, \
      uint32_t(                                             \
          js::ProfilingStackFrame::Flags::LABEL_DETERMINED_BY_CATEGORY_PAIR))

// Similar to AUTO_PROFILER_LABEL_CATEGORY_PAIR but adding the RELEVANT_FOR_JS
// flag.
#define AUTO_PROFILER_LABEL_CATEGORY_PAIR_RELEVANT_FOR_JS(categoryPair)        \
  mozilla::AutoProfilerLabel PROFILER_RAII(                                    \
      "", nullptr, JS::ProfilingCategoryPair::categoryPair,                    \
      uint32_t(                                                                \
          js::ProfilingStackFrame::Flags::LABEL_DETERMINED_BY_CATEGORY_PAIR) | \
          uint32_t(js::ProfilingStackFrame::Flags::RELEVANT_FOR_JS))

// Similar to AUTO_PROFILER_LABEL, but with an additional string. The inserted
// RAII object stores the cStr pointer in a field; it does not copy the string.
//
// WARNING: This means that the string you pass to this macro needs to live at
// least until the end of the current scope. Be careful using this macro with
// ns[C]String; the other AUTO_PROFILER_LABEL_DYNAMIC_* macros below are
// preferred because they avoid this problem.
//
// If the profiler samples the current thread and walks the label stack while
// this RAII object is on the stack, it will copy the supplied string into the
// profile buffer. So there's one string copy operation, and it happens at
// sample time.
//
// Compare this to the plain AUTO_PROFILER_LABEL macro, which only accepts
// literal strings: When the label stack frames generated by
// AUTO_PROFILER_LABEL are sampled, no string copy needs to be made because the
// profile buffer can just store the raw pointers to the literal strings.
// Consequently, AUTO_PROFILER_LABEL frames take up considerably less space in
// the profile buffer than AUTO_PROFILER_LABEL_DYNAMIC_* frames.
#define AUTO_PROFILER_LABEL_DYNAMIC_CSTR(label, categoryPair, cStr) \
  mozilla::AutoProfilerLabel PROFILER_RAII(                         \
      label, cStr, JS::ProfilingCategoryPair::categoryPair)

#define AUTO_PROFILER_LABEL_DYNAMIC_CSTR_RELEVANT_FOR_JS(label, categoryPair, \
                                                         cStr)                \
  mozilla::AutoProfilerLabel PROFILER_RAII(                                   \
      label, cStr, JS::ProfilingCategoryPair::categoryPair,                   \
      uint32_t(js::ProfilingStackFrame::Flags::RELEVANT_FOR_JS))

// Like AUTO_PROFILER_LABEL_DYNAMIC_CSTR, but with the NONSENSITIVE flag to
// note that it does not contain sensitive information (so we can include it
// in, for example, the BackgroundHangMonitor)
#define AUTO_PROFILER_LABEL_DYNAMIC_CSTR_NONSENSITIVE(label, categoryPair, \
                                                      cStr)                \
  mozilla::AutoProfilerLabel PROFILER_RAII(                                \
      label, cStr, JS::ProfilingCategoryPair::categoryPair,                \
      uint32_t(js::ProfilingStackFrame::Flags::NONSENSITIVE))

// Similar to AUTO_PROFILER_LABEL_DYNAMIC_CSTR, but takes an nsACString.
//
// Note: The use of the Maybe<>s ensures the scopes for the dynamic string and
// the AutoProfilerLabel are appropriate, while also not incurring the runtime
// cost of the string assignment unless the profiler is active. Therefore,
// unlike AUTO_PROFILER_LABEL and AUTO_PROFILER_LABEL_DYNAMIC_CSTR, this macro
// doesn't push/pop a label when the profiler is inactive.
#define AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING(label, categoryPair, nsCStr) \
  mozilla::Maybe<nsAutoCString> autoCStr;                                  \
  mozilla::Maybe<mozilla::AutoProfilerLabel> raiiObjectNsCString;          \
  if (profiler_is_active()) {                                              \
    autoCStr.emplace(nsCStr);                                              \
    raiiObjectNsCString.emplace(label, autoCStr->get(),                    \
                                JS::ProfilingCategoryPair::categoryPair);  \
  }

#define AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_RELEVANT_FOR_JS(           \
    label, categoryPair, nsCStr)                                         \
  mozilla::Maybe<nsAutoCString> autoCStr;                                \
  mozilla::Maybe<mozilla::AutoProfilerLabel> raiiObjectNsCString;        \
  if (profiler_is_active()) {                                            \
    autoCStr.emplace(nsCStr);                                            \
    raiiObjectNsCString.emplace(                                         \
        label, autoCStr->get(), JS::ProfilingCategoryPair::categoryPair, \
        uint32_t(js::ProfilingStackFrame::Flags::RELEVANT_FOR_JS));      \
  }

// Match the conditions for MOZ_ENABLE_BACKGROUND_HANG_MONITOR
#if defined(NIGHTLY_BUILD) && !defined(MOZ_DEBUG) && !defined(MOZ_TSAN) && \
    !defined(MOZ_ASAN)
#  define SHOULD_CREATE_ALL_NONSENSITIVE_LABEL_FRAMES true
#else
#  define SHOULD_CREATE_ALL_NONSENSITIVE_LABEL_FRAMES profiler_is_active()
#endif

// See note above AUTO_PROFILER_LABEL_DYNAMIC_CSTR_NONSENSITIVE
#define AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_NONSENSITIVE(              \
    label, categoryPair, nsCStr)                                         \
  mozilla::Maybe<nsAutoCString> autoCStr;                                \
  mozilla::Maybe<mozilla::AutoProfilerLabel> raiiObjectNsCString;        \
  if (SHOULD_CREATE_ALL_NONSENSITIVE_LABEL_FRAMES) {                     \
    autoCStr.emplace(nsCStr);                                            \
    raiiObjectNsCString.emplace(                                         \
        label, autoCStr->get(), JS::ProfilingCategoryPair::categoryPair, \
        uint32_t(js::ProfilingStackFrame::Flags::NONSENSITIVE));         \
  }

// Similar to AUTO_PROFILER_LABEL_DYNAMIC_CSTR, but takes an nsString that is
// is lossily converted to an ASCII string.
//
// Note: The use of the Maybe<>s ensures the scopes for the converted dynamic
// string and the AutoProfilerLabel are appropriate, while also not incurring
// the runtime cost of the string conversion unless the profiler is active.
// Therefore, unlike AUTO_PROFILER_LABEL and AUTO_PROFILER_LABEL_DYNAMIC_CSTR,
// this macro doesn't push/pop a label when the profiler is inactive.
#define AUTO_PROFILER_LABEL_DYNAMIC_LOSSY_NSSTRING(label, categoryPair, nsStr) \
  mozilla::Maybe<NS_LossyConvertUTF16toASCII> asciiStr;                        \
  mozilla::Maybe<mozilla::AutoProfilerLabel> raiiObjectLossyNsString;          \
  if (profiler_is_active()) {                                                  \
    asciiStr.emplace(nsStr);                                                   \
    raiiObjectLossyNsString.emplace(label, asciiStr->get(),                    \
                                    JS::ProfilingCategoryPair::categoryPair);  \
  }

// Similar to AUTO_PROFILER_LABEL, but accepting a JSContext* parameter, and a
// no-op if the profiler is disabled.
// Used to annotate functions for which overhead in the range of nanoseconds is
// noticeable. It avoids overhead from the TLS lookup because it can get the
// ProfilingStack from the JS context, and avoids almost all overhead in the
// case where the profiler is disabled.
#define AUTO_PROFILER_LABEL_FAST(label, categoryPair, ctx) \
  mozilla::AutoProfilerLabelHot PROFILER_RAII(             \
      ctx, label, nullptr, JS::ProfilingCategoryPair::categoryPair)

// Similar to AUTO_PROFILER_LABEL_FAST, but also takes an extra string and an
// additional set of flags. The flags parameter should carry values from the
// js::ProfilingStackFrame::Flags enum.
#define AUTO_PROFILER_LABEL_DYNAMIC_FAST(label, dynamicString, categoryPair, \
                                         ctx, flags)                         \
  mozilla::AutoProfilerLabelHot PROFILER_RAII(                               \
      ctx, label, dynamicString, JS::ProfilingCategoryPair::categoryPair,    \
      flags)

namespace mozilla {

#ifndef MOZ_GECKO_PROFILER

class MOZ_RAII AutoProfilerLabel {
 public:
  // This is the AUTO_PROFILER_LABEL and AUTO_PROFILER_LABEL_DYNAMIC variant.
  AutoProfilerLabel(const char* aLabel, const char* aDynamicString,
                    JS::ProfilingCategoryPair aCategoryPair,
                    uint32_t aFlags = 0) {}

  ~AutoProfilerLabel() {}
};

class MOZ_RAII AutoProfilerLabelHot {
 public:
  // This is the AUTO_PROFILER_LABEL_HOT variant.
  AutoProfilerLabelHot(const char* aLabel, const char* aDynamicString,
                       JS::ProfilingCategoryPair aCategoryPair,
                       uint32_t aFlags = 0) {}

  // This is the AUTO_PROFILER_LABEL_FAST variant.
  AutoProfilerLabelHot(JSContext* aJSContext, const char* aLabel,
                       const char* aDynamicString,
                       JS::ProfilingCategoryPair aCategoryPair,
                       uint32_t aFlags) {}

  ~AutoProfilerLabelHot() {}
};

#else  // !MOZ_GECKO_PROFILER

// This class creates a non-owning ProfilingStack reference. Objects of this
// class are stack-allocated, and so exist within a thread, and are thus bounded
// by the lifetime of the thread, which ensures that the references held can't
// be used after the ProfilingStack is destroyed.
class MOZ_RAII AutoProfilerLabel {
 public:
  // This is the AUTO_PROFILER_LABEL and AUTO_PROFILER_LABEL_DYNAMIC variant.
  AutoProfilerLabel(const char* aLabel, const char* aDynamicString,
                    JS::ProfilingCategoryPair aCategoryPair,
                    uint32_t aFlags = 0) {
    // Get the ProfilingStack from TLS.
    mProfilingStack = profiler::ThreadRegistration::WithOnThreadRefOr(
        [](profiler::ThreadRegistration::OnThreadRef aThread) {
          return &aThread.UnlockedConstReaderAndAtomicRWRef()
                      .ProfilingStackRef();
        },
        nullptr);
    if (mProfilingStack) {
      mProfilingStack->pushLabelFrame(aLabel, aDynamicString, this,
                                      aCategoryPair, aFlags);
    }
  }

  ~AutoProfilerLabel() {
    // This function runs both on and off the main thread.

    if (mProfilingStack) {
      mProfilingStack->pop();
    }
  }

 private:
  // We save a ProfilingStack pointer in the ctor so we don't have to redo the
  // TLS lookup in the dtor.
  ProfilingStack* mProfilingStack;
};

class MOZ_RAII AutoProfilerLabelHot {
 public:
  // This is the AUTO_PROFILER_LABEL_HOT variant. It does nothing if
  // the profiler is inactive.
  AutoProfilerLabelHot(const char* aLabel, const char* aDynamicString,
                       JS::ProfilingCategoryPair aCategoryPair,
                       uint32_t aFlags = 0) {
    if (MOZ_LIKELY(!profiler_is_active())) {
      mProfilingStack = nullptr;
      return;
    }

    // Get the ProfilingStack from TLS.
    mProfilingStack = profiler::ThreadRegistration::WithOnThreadRefOr(
        [](profiler::ThreadRegistration::OnThreadRef aThread) {
          return &aThread.UnlockedConstReaderAndAtomicRWRef()
                      .ProfilingStackRef();
        },
        nullptr);
    if (mProfilingStack) {
      mProfilingStack->pushLabelFrame(aLabel, aDynamicString, this,
                                      aCategoryPair, aFlags);

#  ifdef MOZ_EXECUTION_TRACING
      // We don't have a JSContext in this case, so we don't trace it.
      mCx = nullptr;
#  endif
    }
  }

  // This is the AUTO_PROFILER_LABEL_FAST variant. It retrieves the
  // ProfilingStack from the JSContext and does nothing if the profiler is
  // inactive.
  AutoProfilerLabelHot(JSContext* aJSContext, const char* aLabel,
                       const char* aDynamicString,
                       JS::ProfilingCategoryPair aCategoryPair,
                       uint32_t aFlags) {
    mProfilingStack = js::GetContextProfilingStackIfEnabled(aJSContext);
    if (MOZ_UNLIKELY(mProfilingStack)) {
      mProfilingStack->pushLabelFrame(aLabel, aDynamicString, this,
                                      aCategoryPair, aFlags);
#  ifdef MOZ_EXECUTION_TRACING
      if (MOZ_UNLIKELY(JS_TracerIsTracing(aJSContext))) {
        mCx = aJSContext;
        TraceLabel(aLabel, aDynamicString);
      } else {
        mCx = nullptr;
      }
#  endif
    }
  }

  ~AutoProfilerLabelHot() {
    // This function runs both on and off the main thread.
    if (MOZ_UNLIKELY(mProfilingStack)) {
      mProfilingStack->pop();
#  ifdef MOZ_EXECUTION_TRACING
      if (MOZ_UNLIKELY(mCx)) {
        // We do not bother to produce a detailed label here, and just use an
        // empty string. The label will be lost if we wrap over the ring
        // buffer, but that's fine.
        JS_TracerLeaveLabelLatin1(mCx, "");
      }
#  endif
    }
  }

 private:
#  ifdef MOZ_EXECUTION_TRACING
  MOZ_NEVER_INLINE void TraceLabel(const char* aLabel,
                                   const char* aDynamicString) {
    char buffer[1024];
    SprintfLiteral(buffer, "(DOM) %s.%s", aLabel, aDynamicString);
    JS_TracerEnterLabelLatin1(mCx, buffer);
  }
#  endif

  // We save a ProfilingStack pointer in the ctor so we don't have to redo the
  // TLS lookup in the dtor.
  ProfilingStack* mProfilingStack;

#  ifdef MOZ_EXECUTION_TRACING
  JSContext* mCx;
#  endif
};

#endif  // !MOZ_GECKO_PROFILER

}  // namespace mozilla

#endif  // ProfilerLabels_h
