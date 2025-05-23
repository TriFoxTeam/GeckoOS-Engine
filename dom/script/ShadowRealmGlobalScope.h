/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ShadowRealmGlobalScope_h
#define mozilla_dom_ShadowRealmGlobalScope_h

#include "js/TypeDecls.h"
#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/OriginTrials.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "nsContentUtils.h"
#include "nsIGlobalObject.h"
#include "nsWrapperCache.h"

#include "js/loader/ModuleLoaderBase.h"

namespace mozilla::dom {

#define SHADOWREALMGLOBALSCOPE_IID            \
  {/* 1b0a59dd-c1cb-429a-bb90-cea17994dba2 */ \
   0x1b0a59dd,                                \
   0xc1cb,                                    \
   0x429a,                                    \
   {0xbb, 0x90, 0xce, 0xa1, 0x79, 0x94, 0xdb, 0xa2}}

// Required for providing the wrapper, as this is the global used inside a Gecko
// backed ShadowRealm, but also required to power module resolution.
class ShadowRealmGlobalScope final : public nsIGlobalObject,
                                     public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ShadowRealmGlobalScope)

  NS_INLINE_DECL_STATIC_IID(SHADOWREALMGLOBALSCOPE_IID)

  explicit ShadowRealmGlobalScope(nsIGlobalObject* aCreatingGlobal)
      : mCreatingGlobal(aCreatingGlobal) {};

  nsIGlobalObject* GetCreatingGlobal() const { return mCreatingGlobal; }
  OriginTrials Trials() const override { return {}; }

  JSObject* GetGlobalJSObject() override { return GetWrapper(); }
  JSObject* GetGlobalJSObjectPreserveColor() const override {
    return GetWrapperPreserveColor();
  }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override {
    MOZ_CRASH("Shouldn't be here");
    return nullptr;
  }

  JS::loader::ModuleLoaderBase* GetModuleLoader(JSContext* aCx) override;

  bool ShouldResistFingerprinting(RFPTarget aTarget) const override {
    return nsContentUtils::ShouldResistFingerprinting(
        "Presently we don't have enough context to make an informed decision"
        "on JS Sandboxes. See 1782853",
        aTarget);
  }

  nsISerialEventTarget* SerialEventTarget() const final {
    return mozilla::GetMainThreadSerialEventTarget();
  }
  nsresult Dispatch(already_AddRefed<nsIRunnable>&& aRunnable) const final {
    return mozilla::SchedulerGroup::Dispatch(std::move(aRunnable));
  }

 private:
  virtual ~ShadowRealmGlobalScope() = default;

  RefPtr<JS::loader::ModuleLoaderBase> mModuleLoader;

  // The global which created this ShadowRealm
  nsCOMPtr<nsIGlobalObject> mCreatingGlobal;
};

JSObject* NewShadowRealmGlobal(JSContext* aCx, JS::RealmOptions& aOptions,
                               JSPrincipals* aPrincipals,
                               JS::Handle<JSObject*> aGlobalObj);

bool IsShadowRealmGlobal(JSObject* aObject);

}  // namespace mozilla::dom

#endif
