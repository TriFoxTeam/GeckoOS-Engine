/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_SVGANIMATEDCLASSORSTRING_H_
#define DOM_SVG_SVGANIMATEDCLASSORSTRING_H_

#include "nsStringFwd.h"
#include "mozilla/AlreadyAddRefed.h"

class nsIPrincipal;

namespace mozilla {

class ErrorResult;

namespace dom {
class DOMSVGAnimatedString;
class SVGElement;
class OwningTrustedScriptURLOrString;
class TrustedScriptURLOrString;
}  // namespace dom

class SVGAnimatedClassOrString {
 public:
  using SVGElement = dom::SVGElement;
  using OwningTrustedScriptURLOrString = dom::OwningTrustedScriptURLOrString;
  using TrustedScriptURLOrString = dom::TrustedScriptURLOrString;

  virtual void SetBaseValue(const nsAString& aValue, SVGElement* aSVGElement,
                            bool aDoSetAttr) = 0;
  MOZ_CAN_RUN_SCRIPT virtual void SetBaseValue(
      const TrustedScriptURLOrString& aValue, SVGElement* aSVGElement,
      bool aDoSetAttr, nsIPrincipal* aSubjectPrincipal, ErrorResult& aRv);
  virtual void GetBaseValue(nsAString& aValue,
                            const SVGElement* aSVGElement) const = 0;
  virtual void GetBaseValue(OwningTrustedScriptURLOrString& aValue,
                            const SVGElement* aSVGElement) const;

  virtual void GetAnimValue(nsAString& aResult,
                            const SVGElement* aSVGElement) const = 0;

  already_AddRefed<dom::DOMSVGAnimatedString> ToDOMAnimatedString(
      SVGElement* aSVGElement);

  void RemoveTearoff();
};

}  // namespace mozilla

#endif  // DOM_SVG_SVGANIMATEDCLASSORSTRING_H_
