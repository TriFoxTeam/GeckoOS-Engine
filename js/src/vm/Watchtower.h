/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Watchtower_h
#define vm_Watchtower_h

#include "js/TypeDecls.h"
#include "vm/NativeObject.h"

namespace js {

// [SMDOC] Watchtower
//
// Watchtower is a framework to hook into changes to certain objects. This gives
// us the ability to, for instance, invalidate caches or purge Warp code on
// object layout changes.
//
// Watchtower is only used for objects with certain ObjectFlags set on the
// Shape. This minimizes performance overhead for most objects.
//
// We currently use Watchtower for:
//
// - Invalidating the shape teleporting optimization. See the "Shape Teleporting
//   Optimization" SMDOC comment in CacheIR.cpp.
//
// - Invalidating the MegamorphicCache, a property lookup cache for megamorphic
//   property accesses. See the SMDOC comment in vm/Caches.h.
//
// There's also a testing mechanism that lets us write tests for Watchtower
// hooks. See setWatchtowerCallback and addWatchtowerTarget defined in
// TestingFunctions.cpp.
class Watchtower {
  static bool watchPropertyAddSlow(JSContext* cx, Handle<NativeObject*> obj,
                                   HandleId id);
  static bool watchPropertyRemoveSlow(JSContext* cx, Handle<NativeObject*> obj,
                                      HandleId id);
  static bool watchPropertyFlagsChangeSlow(JSContext* cx,
                                           Handle<NativeObject*> obj,
                                           HandleId id, PropertyInfo propInfo,
                                           PropertyFlags newFlags);
  template <AllowGC allowGC>
  static void watchPropertyValueChangeSlow(
      JSContext* cx,
      typename MaybeRooted<NativeObject*, allowGC>::HandleType obj,
      typename MaybeRooted<PropertyKey, allowGC>::HandleType id,
      typename MaybeRooted<Value, allowGC>::HandleType value,
      PropertyInfo propInfo);
  static bool watchFreezeOrSealSlow(JSContext* cx, Handle<NativeObject*> obj,
                                    IntegrityLevel level);
  static bool watchProtoChangeSlow(JSContext* cx, HandleObject obj);
  static bool watchObjectSwapSlow(JSContext* cx, HandleObject a,
                                  HandleObject b);

 public:
  static bool watchesPropertyAdd(NativeObject* obj) {
    return obj->hasAnyFlag(
        {ObjectFlag::IsUsedAsPrototype, ObjectFlag::UseWatchtowerTestingLog});
  }
  static bool watchesPropertyRemove(NativeObject* obj) {
    return obj->hasAnyFlag(
        {ObjectFlag::IsUsedAsPrototype, ObjectFlag::GenerationCountedGlobal,
         ObjectFlag::UseWatchtowerTestingLog, ObjectFlag::HasFuseProperty});
  }
  static bool watchesPropertyFlagsChange(NativeObject* obj) {
    return obj->hasAnyFlag({ObjectFlag::IsUsedAsPrototype,
                            ObjectFlag::GenerationCountedGlobal,
                            ObjectFlag::UseWatchtowerTestingLog});
  }
  static bool watchesPropertyValueChange(NativeObject* obj) {
    return obj->hasAnyFlag(
        {ObjectFlag::HasFuseProperty, ObjectFlag::UseWatchtowerTestingLog});
  }
  static bool watchesFreezeOrSeal(NativeObject* obj) {
    return obj->hasAnyFlag(
        {ObjectFlag::IsUsedAsPrototype, ObjectFlag::UseWatchtowerTestingLog});
  }
  static bool watchesProtoChange(JSObject* obj) {
    return obj->hasAnyFlag(
        {ObjectFlag::IsUsedAsPrototype, ObjectFlag::UseWatchtowerTestingLog});
  }
  static bool watchesObjectSwap(JSObject* a, JSObject* b) {
    auto watches = [](JSObject* obj) {
      return obj->hasAnyFlag(
          {ObjectFlag::IsUsedAsPrototype, ObjectFlag::UseWatchtowerTestingLog});
    };
    return watches(a) || watches(b);
  }

  static bool watchPropertyAdd(JSContext* cx, Handle<NativeObject*> obj,
                               HandleId id) {
    if (MOZ_LIKELY(!watchesPropertyAdd(obj))) {
      return true;
    }
    return watchPropertyAddSlow(cx, obj, id);
  }
  static bool watchPropertyRemove(JSContext* cx, Handle<NativeObject*> obj,
                                  HandleId id) {
    if (MOZ_LIKELY(!watchesPropertyRemove(obj))) {
      return true;
    }
    return watchPropertyRemoveSlow(cx, obj, id);
  }
  static bool watchPropertyFlagsChange(JSContext* cx, Handle<NativeObject*> obj,
                                       HandleId id, PropertyInfo propInfo,
                                       PropertyFlags newFlags) {
    if (MOZ_LIKELY(!watchesPropertyFlagsChange(obj))) {
      return true;
    }
    return watchPropertyFlagsChangeSlow(cx, obj, id, propInfo, newFlags);
  }

  // Note: We can only watch property value changes for regular object slots
  // with an id, not reserved slots.
  template <AllowGC allowGC>
  static void watchPropertyValueChange(
      JSContext* cx,
      typename MaybeRooted<NativeObject*, allowGC>::HandleType obj,
      typename MaybeRooted<PropertyKey, allowGC>::HandleType id,
      typename MaybeRooted<Value, allowGC>::HandleType value,
      PropertyInfo propInfo) {
    if (MOZ_LIKELY(!watchesPropertyValueChange(obj))) {
      return;
    }
    watchPropertyValueChangeSlow<allowGC>(cx, obj, id, value, propInfo);
  }
  static bool watchFreezeOrSeal(JSContext* cx, Handle<NativeObject*> obj,
                                IntegrityLevel level) {
    if (MOZ_LIKELY(!watchesFreezeOrSeal(obj))) {
      return true;
    }
    return watchFreezeOrSealSlow(cx, obj, level);
  }
  static bool watchProtoChange(JSContext* cx, HandleObject obj) {
    if (MOZ_LIKELY(!watchesProtoChange(obj))) {
      return true;
    }
    return watchProtoChangeSlow(cx, obj);
  }

  static bool watchObjectSwap(JSContext* cx, HandleObject a, HandleObject b) {
    if (MOZ_LIKELY(!watchesObjectSwap(a, b))) {
      return true;
    }
    return watchObjectSwapSlow(cx, a, b);
  }
};

}  // namespace js

#endif /* vm_Watchtower_h */
