/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
// This is also necessary to ensure our definition of M_SQRT1_2 is picked up
#include "SVGContentUtils.h"

// Keep others in (case-insensitive) order:
#include "gfx2DGlue.h"
#include "gfxMatrix.h"
#include "gfxPlatform.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "mozilla/PresShell.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGContextPaint.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/TextUtils.h"
#include "nsComputedDOMStyle.h"
#include "nsContainerFrame.h"
#include "nsFontMetrics.h"
#include "nsIFrame.h"
#include "nsIScriptError.h"
#include "nsLayoutUtils.h"
#include "nsMathUtils.h"
#include "nsWhitespaceTokenizer.h"
#include "SVGAnimatedPreserveAspectRatio.h"
#include "SVGGeometryProperty.h"
#include "nsContentUtils.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/ComputedStyle.h"
#include "SVGOuterSVGFrame.h"
#include "SVGPathData.h"
#include "SVGPathElement.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::dom::SVGPreserveAspectRatio_Binding;
using namespace mozilla::gfx;

static bool StringToValue(const nsAString& aString, float& aValue) {
  nsresult errorCode;
  aValue = aString.ToFloat(&errorCode);
  return NS_SUCCEEDED(errorCode);
}

static bool StringToValue(const nsAString& aString, double& aValue) {
  nsresult errorCode;
  aValue = aString.ToDouble(&errorCode);
  return NS_SUCCEEDED(errorCode);
}

namespace mozilla {

SVGSVGElement* SVGContentUtils::GetOuterSVGElement(SVGElement* aSVGElement) {
  Element* element = nullptr;
  Element* ancestor = aSVGElement->GetParentElementCrossingShadowRoot();

  while (ancestor && ancestor->IsSVGElement() &&
         !ancestor->IsSVGElement(nsGkAtoms::foreignObject)) {
    element = ancestor;
    ancestor = element->GetParentElementCrossingShadowRoot();
  }

  return SVGSVGElement::FromNodeOrNull(element);
}

enum DashState {
  eDashedStroke,
  eContinuousStroke,  //< all dashes, no gaps
  eNoStroke           //< all gaps, no dashes
};

static DashState GetStrokeDashData(
    SVGContentUtils::AutoStrokeOptions* aStrokeOptions, SVGElement* aElement,
    const nsStyleSVG* aStyleSVG, const SVGContextPaint* aContextPaint) {
  size_t dashArrayLength;
  Float totalLengthOfDashes = 0.0, totalLengthOfGaps = 0.0;
  Float pathScale = 1.0;

  if (aStyleSVG->mStrokeDasharray.IsContextValue()) {
    if (!aContextPaint) {
      return eContinuousStroke;
    }
    const FallibleTArray<Float>& dashSrc = aContextPaint->GetStrokeDashArray();
    dashArrayLength = dashSrc.Length();
    if (dashArrayLength <= 0) {
      return eContinuousStroke;
    }
    Float* dashPattern = aStrokeOptions->InitDashPattern(dashArrayLength);
    if (!dashPattern) {
      return eContinuousStroke;
    }
    for (size_t i = 0; i < dashArrayLength; i++) {
      if (dashSrc[i] < 0.0) {
        return eContinuousStroke;  // invalid
      }
      dashPattern[i] = Float(dashSrc[i]);
      (i % 2 ? totalLengthOfGaps : totalLengthOfDashes) += dashSrc[i];
    }
  } else {
    const auto dasharray = aStyleSVG->mStrokeDasharray.AsValues().AsSpan();
    dashArrayLength = dasharray.Length();
    if (dashArrayLength <= 0) {
      return eContinuousStroke;
    }
    if (auto* shapeElement = SVGGeometryElement::FromNode(aElement)) {
      pathScale =
          shapeElement->GetPathLengthScale(SVGGeometryElement::eForStroking);
      if (pathScale <= 0 || !std::isfinite(pathScale)) {
        return eContinuousStroke;
      }
    }
    Float* dashPattern = aStrokeOptions->InitDashPattern(dashArrayLength);
    if (!dashPattern) {
      return eContinuousStroke;
    }
    for (uint32_t i = 0; i < dashArrayLength; i++) {
      Float dashLength =
          SVGContentUtils::CoordToFloat(aElement, dasharray[i]) * pathScale;
      if (dashLength < 0.0) {
        return eContinuousStroke;  // invalid
      }
      dashPattern[i] = dashLength;
      (i % 2 ? totalLengthOfGaps : totalLengthOfDashes) += dashLength;
    }
  }

  // Now that aStrokeOptions.mDashPattern is fully initialized (we didn't
  // return early above) we can safely set mDashLength:
  aStrokeOptions->mDashLength = dashArrayLength;

  if ((dashArrayLength % 2) == 1) {
    // If we have a dash pattern with an odd number of lengths the pattern
    // repeats a second time, per the SVG spec., and as implemented by Moz2D.
    // When deciding whether to return eNoStroke or eContinuousStroke below we
    // need to take into account that in the repeat pattern the dashes become
    // gaps, and the gaps become dashes.
    Float origTotalLengthOfDashes = totalLengthOfDashes;
    totalLengthOfDashes += totalLengthOfGaps;
    totalLengthOfGaps += origTotalLengthOfDashes;
  }

  // Stroking using dashes is much slower than stroking a continuous line
  // (see bug 609361 comment 40), and much, much slower than not stroking the
  // line at all. Here we check for cases when the dash pattern causes the
  // stroke to essentially be continuous or to be nonexistent in which case
  // we can avoid expensive stroking operations (the underlying platform
  // graphics libraries don't seem to optimize for this).
  if (totalLengthOfGaps <= 0) {
    return eContinuousStroke;
  }
  // We can only return eNoStroke if the value of stroke-linecap isn't
  // adding caps to zero length dashes.
  if (totalLengthOfDashes <= 0 &&
      aStyleSVG->mStrokeLinecap == StyleStrokeLinecap::Butt) {
    return eNoStroke;
  }

  if (aStyleSVG->mStrokeDashoffset.IsContextValue()) {
    aStrokeOptions->mDashOffset =
        Float(aContextPaint ? aContextPaint->GetStrokeDashOffset() : 0);
  } else {
    aStrokeOptions->mDashOffset =
        SVGContentUtils::CoordToFloat(
            aElement, aStyleSVG->mStrokeDashoffset.AsLengthPercentage()) *
        pathScale;
  }

  return eDashedStroke;
}

void SVGContentUtils::GetStrokeOptions(AutoStrokeOptions* aStrokeOptions,
                                       SVGElement* aElement,
                                       const ComputedStyle* aComputedStyle,
                                       const SVGContextPaint* aContextPaint,
                                       StrokeOptionFlags aFlags) {
  auto doCompute = [&](const ComputedStyle* computedStyle) {
    const nsStyleSVG* styleSVG = computedStyle->StyleSVG();

    bool checkedDashAndStrokeIsDashed = false;
    if (aFlags != eIgnoreStrokeDashing) {
      DashState dashState =
          GetStrokeDashData(aStrokeOptions, aElement, styleSVG, aContextPaint);

      if (dashState == eNoStroke) {
        // Hopefully this will shortcircuit any stroke operations:
        aStrokeOptions->mLineWidth = 0;
        return;
      }
      if (dashState == eContinuousStroke && aStrokeOptions->mDashPattern) {
        // Prevent our caller from wasting time looking at a pattern without
        // gaps:
        aStrokeOptions->DiscardDashPattern();
      }
      checkedDashAndStrokeIsDashed = (dashState == eDashedStroke);
    }

    aStrokeOptions->mLineWidth =
        GetStrokeWidth(aElement, computedStyle, aContextPaint);

    aStrokeOptions->mMiterLimit = Float(styleSVG->mStrokeMiterlimit);

    switch (styleSVG->mStrokeLinejoin) {
      case StyleStrokeLinejoin::Miter:
        aStrokeOptions->mLineJoin = JoinStyle::MITER_OR_BEVEL;
        break;
      case StyleStrokeLinejoin::Round:
        aStrokeOptions->mLineJoin = JoinStyle::ROUND;
        break;
      case StyleStrokeLinejoin::Bevel:
        aStrokeOptions->mLineJoin = JoinStyle::BEVEL;
        break;
    }

    if (ShapeTypeHasNoCorners(aElement) && !checkedDashAndStrokeIsDashed) {
      // Note: if aFlags == eIgnoreStrokeDashing then we may be returning the
      // wrong linecap value here, since the actual linecap used on render in
      // this case depends on whether the stroke is dashed or not.
      aStrokeOptions->mLineCap = CapStyle::BUTT;
    } else {
      switch (styleSVG->mStrokeLinecap) {
        case StyleStrokeLinecap::Butt:
          aStrokeOptions->mLineCap = CapStyle::BUTT;
          break;
        case StyleStrokeLinecap::Round:
          aStrokeOptions->mLineCap = CapStyle::ROUND;
          break;
        case StyleStrokeLinecap::Square:
          aStrokeOptions->mLineCap = CapStyle::SQUARE;
          break;
      }
    }
  };

  if (aComputedStyle) {
    doCompute(aComputedStyle);
  } else {
    SVGGeometryProperty::DoForComputedStyle(aElement, doCompute);
  }
}

Float SVGContentUtils::GetStrokeWidth(const SVGElement* aElement,
                                      const ComputedStyle* aComputedStyle,
                                      const SVGContextPaint* aContextPaint) {
  Float res = 0.0;

  auto doCompute = [&](const ComputedStyle* computedStyle) {
    const nsStyleSVG* styleSVG = computedStyle->StyleSVG();

    if (styleSVG->mStrokeWidth.IsContextValue()) {
      res = aContextPaint ? aContextPaint->GetStrokeWidth() : 1.0;
    } else {
      auto& lp = styleSVG->mStrokeWidth.AsLengthPercentage();
      if (lp.HasPercent() && aElement) {
        auto counter =
            aElement->IsSVGElement(nsGkAtoms::text)
                ? UseCounter::eUseCounter_custom_PercentageStrokeWidthInSVGText
                : UseCounter::eUseCounter_custom_PercentageStrokeWidthInSVG;
        aElement->OwnerDoc()->SetUseCounter(counter);
      }
      res = SVGContentUtils::CoordToFloat(aElement, lp);
    }
  };

  if (aComputedStyle) {
    doCompute(aComputedStyle);
  } else {
    SVGGeometryProperty::DoForComputedStyle(aElement, doCompute);
  }

  return res;
}

float SVGContentUtils::GetFontSize(const Element* aElement) {
  if (!aElement) {
    return 1.0f;
  }

  nsPresContext* pc = nsContentUtils::GetContextForContent(aElement);
  if (!pc) {
    return 1.0f;
  }

  if (auto* f = aElement->GetPrimaryFrame()) {
    return GetFontSize(f->Style(), pc);
  }

  if (RefPtr<const ComputedStyle> style =
          nsComputedDOMStyle::GetComputedStyleNoFlush(aElement)) {
    return GetFontSize(style, pc);
  }

  // ReportToConsole
  NS_WARNING("Couldn't get ComputedStyle for content in GetFontStyle");
  return 1.0f;
}

float SVGContentUtils::GetFontSize(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "NULL frame in GetFontSize");
  return GetFontSize(aFrame->Style(), aFrame->PresContext());
}

float SVGContentUtils::GetFontSize(const ComputedStyle* aComputedStyle,
                                   nsPresContext* aPresContext) {
  MOZ_ASSERT(aComputedStyle);
  MOZ_ASSERT(aPresContext);

  return aComputedStyle->StyleFont()->mSize.ToCSSPixels() /
         aPresContext->TextZoom();
}

float SVGContentUtils::GetFontXHeight(const Element* aElement) {
  if (!aElement) {
    return 1.0f;
  }

  nsPresContext* pc = nsContentUtils::GetContextForContent(aElement);
  if (!pc) {
    return 1.0f;
  }

  if (auto* f = aElement->GetPrimaryFrame()) {
    return GetFontXHeight(f->Style(), pc);
  }

  if (RefPtr<const ComputedStyle> style =
          nsComputedDOMStyle::GetComputedStyleNoFlush(aElement)) {
    return GetFontXHeight(style, pc);
  }

  // ReportToConsole
  NS_WARNING("Couldn't get ComputedStyle for content in GetFontStyle");
  return 1.0f;
}

float SVGContentUtils::GetFontXHeight(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "NULL frame in GetFontXHeight");
  return GetFontXHeight(aFrame->Style(), aFrame->PresContext());
}

float SVGContentUtils::GetFontXHeight(const ComputedStyle* aComputedStyle,
                                      nsPresContext* aPresContext) {
  MOZ_ASSERT(aComputedStyle && aPresContext);

  RefPtr<nsFontMetrics> fontMetrics =
      nsLayoutUtils::GetFontMetricsForComputedStyle(aComputedStyle,
                                                    aPresContext);

  if (!fontMetrics) {
    // ReportToConsole
    NS_WARNING("no FontMetrics in GetFontXHeight()");
    return 1.0f;
  }

  nscoord xHeight = fontMetrics->XHeight();
  return nsPresContext::AppUnitsToFloatCSSPixels(xHeight) /
         aPresContext->TextZoom();
}

float SVGContentUtils::GetLineHeight(const Element* aElement) {
  float result = 16.0f * ReflowInput::kNormalLineHeightFactor;
  if (!aElement) {
    return result;
  }
  SVGGeometryProperty::DoForComputedStyle(
      aElement, [&](const ComputedStyle* style) {
        auto* context = nsContentUtils::GetContextForContent(aElement);
        if (!context) {
          return;
        }
        const auto lineHeightAu = ReflowInput::CalcLineHeight(
            *style, context, aElement, NS_UNCONSTRAINEDSIZE, 1.0f);
        result = CSSPixel::FromAppUnits(lineHeightAu);
      });

  return result;
}

nsresult SVGContentUtils::ReportToConsole(const Document* doc,
                                          const char* aWarning,
                                          const nsTArray<nsString>& aParams) {
  return nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "SVG"_ns,
                                         doc, nsContentUtils::eSVG_PROPERTIES,
                                         aWarning, aParams);
}

static bool EstablishesViewport(const nsIContent* aContent) {
  MOZ_ASSERT(aContent, "Expecting aContent to be non-null");

  // A symbol element only establishes a viewport if it is instanced by a use
  // element.
  if (aContent->IsSVGElement(nsGkAtoms::symbol) &&
      aContent->IsInSVGUseShadowTree()) {
    return true;
  }
  return aContent->IsSVGElement(nsGkAtoms::svg);
}

SVGViewportElement* SVGContentUtils::GetNearestViewportElement(
    const nsIContent* aContent) {
  nsIContent* element = aContent->GetFlattenedTreeParent();

  while (element && element->IsSVGElement()) {
    if (element->IsSVGElement(nsGkAtoms::foreignObject)) {
      return nullptr;
    }
    if (EstablishesViewport(element)) {
      return static_cast<SVGViewportElement*>(element);
    }
    element = element->GetFlattenedTreeParent();
  }
  return nullptr;
}

enum class CTMType { NearestViewport, NonScalingStroke, Screen };

static gfx::Matrix GetCTMInternal(SVGElement* aElement, CTMType aCTMType,
                                  bool aHaveRecursed) {
  auto getLocalTransformHelper =
      [](SVGElement const* e, bool shouldIncludeChildToUserSpace) -> gfxMatrix {
    gfxMatrix ret;
    if (auto* f = e->GetPrimaryFrame()) {
      ret = SVGUtils::GetTransformMatrixInUserSpace(f);
    }
    if (shouldIncludeChildToUserSpace) {
      ret = e->ChildToUserSpaceTransform() * ret;
    }
    return ret;
  };

  auto postTranslateFrameOffset = [](nsIFrame* aFrame, nsIFrame* aAncestorFrame,
                                     gfx::Matrix& aMatrix) {
    auto point = aFrame->GetOffsetTo(aAncestorFrame);
    aMatrix =
        aMatrix.PostTranslate(nsPresContext::AppUnitsToFloatCSSPixels(point.x),
                              nsPresContext::AppUnitsToFloatCSSPixels(point.y));
  };

  gfxMatrix matrix = getLocalTransformHelper(aElement, aHaveRecursed);

  SVGElement* element = aElement;
  nsIContent* ancestor = aElement->GetFlattenedTreeParent();

  while (ancestor && ancestor->IsSVGElement() &&
         !ancestor->IsSVGElement(nsGkAtoms::foreignObject)) {
    element = static_cast<SVGElement*>(ancestor);
    if (aCTMType == CTMType::NonScalingStroke) {
      if (auto* el = SVGSVGElement::FromNode(element); el && !el->IsInner()) {
        if (SVGOuterSVGFrame* frame =
                do_QueryFrame(element->GetPrimaryFrame())) {
          Matrix childTransform;
          if (frame->HasChildrenOnlyTransform(&childTransform)) {
            return gfx::ToMatrix(matrix) * childTransform;
          }
        }
        return gfx::ToMatrix(matrix);
      }
    }
    matrix *= getLocalTransformHelper(element, true);
    if (aCTMType == CTMType::NearestViewport) {
      if (element->IsSVGElement(nsGkAtoms::foreignObject)) {
        return gfx::Matrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);  // singular
      }
      if (EstablishesViewport(element)) {
        // XXX spec seems to say x,y translation should be undone for IsInnerSVG
        return gfx::ToMatrix(matrix);
      }
    }
    ancestor = ancestor->GetFlattenedTreeParent();
  }
  if (aCTMType == CTMType::NearestViewport) {
    // didn't find a nearestViewportElement
    return gfx::Matrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);  // singular
  }
  if (!element->IsSVGElement(nsGkAtoms::svg)) {
    // Not a valid SVG fragment
    return gfx::Matrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);  // singular
  }
  if (element == aElement && !aHaveRecursed) {
    // We get here when getScreenCTM() is called on an outer-<svg>.
    // Consistency with other elements would have us include only the
    // eFromUserSpace transforms, but we include the eAllTransforms
    // transforms in this case since that's what we've been doing for
    // a while, and it keeps us consistent with WebKit and Opera (if not
    // really with the ambiguous spec).
    matrix = getLocalTransformHelper(aElement, true);
  }

  gfx::Matrix tm = gfx::ToMatrix(matrix);
  nsIFrame* frame = element->GetPrimaryFrame();
  if (!frame) {
    return tm;
  }
  if (frame->IsSVGOuterSVGFrame()) {
    nsMargin bp = frame->GetUsedBorderAndPadding();
    int32_t appUnitsPerCSSPixel = AppUnitsPerCSSPixel();
    float xOffset = NSAppUnitsToFloatPixels(bp.left, appUnitsPerCSSPixel);
    float yOffset = NSAppUnitsToFloatPixels(bp.top, appUnitsPerCSSPixel);
    // See
    // https://drafts.csswg.org/css-transforms/#valdef-transform-box-fill-box
    // For elements with associated CSS layout box, the used value for fill-box
    // is content-box and for stroke-box and view-box is border-box.
    switch (frame->StyleDisplay()->mTransformBox) {
      case StyleTransformBox::FillBox:
      case StyleTransformBox::ContentBox:
        // Apply border/padding separate from the rest of the transform.
        // i.e. after it's been transformed
        tm.PostTranslate(xOffset, yOffset);
        break;
      case StyleTransformBox::StrokeBox:
      case StyleTransformBox::ViewBox:
      case StyleTransformBox::BorderBox:
        // Apply border/padding before we transform the surface.
        tm.PreTranslate(xOffset, yOffset);
        break;
    }
  }

  if (!ancestor || !ancestor->IsElement()) {
    return tm;
  }
  if (auto* ancestorSVG = SVGElement::FromNode(ancestor)) {
    return tm * GetCTMInternal(ancestorSVG, aCTMType, true);
  }
  nsIFrame* parentFrame = frame->GetParent();
  if (!parentFrame) {
    return tm;
  }
  postTranslateFrameOffset(frame, parentFrame, tm);

  nsIContent* nearestSVGAncestor = ancestor;
  while (nearestSVGAncestor && !nearestSVGAncestor->IsSVGElement()) {
    nearestSVGAncestor = nearestSVGAncestor->GetFlattenedTreeParent();
  }

  nsIFrame* ancestorFrame;
  if (nearestSVGAncestor) {
    ancestorFrame = nearestSVGAncestor->GetPrimaryFrame();
  } else {
    Document* currentDoc = aElement->GetComposedDoc();
    PresShell* presShell = currentDoc ? currentDoc->GetPresShell() : nullptr;
    ancestorFrame = presShell ? presShell->GetRootFrame() : nullptr;
  }
  if (!ancestorFrame) {
    return tm;
  }
  auto transformToAncestor = nsLayoutUtils::GetTransformToAncestor(
      RelativeTo{parentFrame, ViewportType::Layout},
      RelativeTo{ancestorFrame, ViewportType::Layout}, nsIFrame::IN_CSS_UNITS);
  gfx::Matrix result2d;
  if (transformToAncestor.CanDraw2D(&result2d)) {
    tm = tm * result2d;
  } else {
    // The transform from our outer SVG matrix to the root is a 3D
    // transform. We can't really process that so give up and just
    // return the overall translation from the outer SVG to the root.
    postTranslateFrameOffset(parentFrame, ancestorFrame, tm);
  }
  return nearestSVGAncestor
             ? tm * GetCTMInternal(static_cast<SVGElement*>(nearestSVGAncestor),
                                   aCTMType, true)
             : tm;
}

gfx::Matrix SVGContentUtils::GetCTM(SVGElement* aElement) {
  return GetCTMInternal(aElement, CTMType::NearestViewport, false);
}

gfx::Matrix SVGContentUtils::GetNonScalingStrokeCTM(SVGElement* aElement) {
  return GetCTMInternal(aElement, CTMType::NonScalingStroke, false);
}

gfx::Matrix SVGContentUtils::GetScreenCTM(SVGElement* aElement) {
  return GetCTMInternal(aElement, CTMType::Screen, false);
}

void SVGContentUtils::RectilinearGetStrokeBounds(
    const Rect& aRect, const Matrix& aToBoundsSpace,
    const Matrix& aToNonScalingStrokeSpace, float aStrokeWidth, Rect* aBounds) {
  MOZ_ASSERT(aToBoundsSpace.IsRectilinear(),
             "aToBoundsSpace must be rectilinear");
  MOZ_ASSERT(aToNonScalingStrokeSpace.IsRectilinear(),
             "aToNonScalingStrokeSpace must be rectilinear");

  Matrix nonScalingToSource = aToNonScalingStrokeSpace.Inverse();
  Matrix nonScalingToBounds = nonScalingToSource * aToBoundsSpace;

  *aBounds = aToBoundsSpace.TransformBounds(aRect);

  // Compute the amounts dx and dy that nonScalingToBounds scales a half-width
  // stroke in the x and y directions, and then inflate aBounds by those amounts
  // so that when aBounds is transformed back to non-scaling-stroke space
  // it will map onto the correct stroked bounds.

  Float dx = 0.0f;
  Float dy = 0.0f;
  // nonScalingToBounds is rectilinear, so either _12 and _21 are zero or _11
  // and _22 are zero, and in each case the non-zero entries (from among _11,
  // _12, _21, _22) simply scale the stroke width in the x and y directions.
  if (FuzzyEqual(nonScalingToBounds._12, 0) &&
      FuzzyEqual(nonScalingToBounds._21, 0)) {
    dx = (aStrokeWidth / 2.0f) * std::abs(nonScalingToBounds._11);
    dy = (aStrokeWidth / 2.0f) * std::abs(nonScalingToBounds._22);
  } else {
    dx = (aStrokeWidth / 2.0f) * std::abs(nonScalingToBounds._21);
    dy = (aStrokeWidth / 2.0f) * std::abs(nonScalingToBounds._12);
  }

  aBounds->Inflate(dx, dy);
}

double SVGContentUtils::ComputeNormalizedHypotenuse(double aWidth,
                                                    double aHeight) {
  return NS_hypot(aWidth, aHeight) / M_SQRT2;
}

float SVGContentUtils::AngleBisect(float a1, float a2) {
  float delta = std::fmod(a2 - a1, static_cast<float>(2 * M_PI));
  if (delta < 0) {
    delta += static_cast<float>(2 * M_PI);
  }
  /* delta is now the angle from a1 around to a2, in the range [0, 2*M_PI) */
  float r = a1 + delta / 2;
  if (delta >= M_PI) {
    /* the arc from a2 to a1 is smaller, so use the ray on that side */
    r += static_cast<float>(M_PI);
  }
  return r;
}

gfx::Matrix SVGContentUtils::GetViewBoxTransform(
    float aViewportWidth, float aViewportHeight, float aViewboxX,
    float aViewboxY, float aViewboxWidth, float aViewboxHeight,
    const SVGAnimatedPreserveAspectRatio& aPreserveAspectRatio) {
  return GetViewBoxTransform(aViewportWidth, aViewportHeight, aViewboxX,
                             aViewboxY, aViewboxWidth, aViewboxHeight,
                             aPreserveAspectRatio.GetAnimValue());
}

gfx::Matrix SVGContentUtils::GetViewBoxTransform(
    float aViewportWidth, float aViewportHeight, float aViewboxX,
    float aViewboxY, float aViewboxWidth, float aViewboxHeight,
    const SVGPreserveAspectRatio& aPreserveAspectRatio) {
  NS_ASSERTION(aViewportWidth >= 0, "viewport width must be nonnegative!");
  NS_ASSERTION(aViewportHeight >= 0, "viewport height must be nonnegative!");
  NS_ASSERTION(aViewboxWidth > 0, "viewBox width must be greater than zero!");
  NS_ASSERTION(aViewboxHeight > 0, "viewBox height must be greater than zero!");

  uint16_t align = aPreserveAspectRatio.GetAlign();
  uint16_t meetOrSlice = aPreserveAspectRatio.GetMeetOrSlice();

  // default to the defaults
  if (align == SVG_PRESERVEASPECTRATIO_UNKNOWN)
    align = SVG_PRESERVEASPECTRATIO_XMIDYMID;
  if (meetOrSlice == SVG_MEETORSLICE_UNKNOWN)
    meetOrSlice = SVG_MEETORSLICE_MEET;

  float a, d, e, f;
  a = aViewportWidth / aViewboxWidth;
  d = aViewportHeight / aViewboxHeight;
  e = 0.0f;
  f = 0.0f;

  if (align != SVG_PRESERVEASPECTRATIO_NONE && a != d) {
    if ((meetOrSlice == SVG_MEETORSLICE_MEET && a < d) ||
        (meetOrSlice == SVG_MEETORSLICE_SLICE && d < a)) {
      d = a;
      switch (align) {
        case SVG_PRESERVEASPECTRATIO_XMINYMIN:
        case SVG_PRESERVEASPECTRATIO_XMIDYMIN:
        case SVG_PRESERVEASPECTRATIO_XMAXYMIN:
          break;
        case SVG_PRESERVEASPECTRATIO_XMINYMID:
        case SVG_PRESERVEASPECTRATIO_XMIDYMID:
        case SVG_PRESERVEASPECTRATIO_XMAXYMID:
          f = (aViewportHeight - a * aViewboxHeight) / 2.0f;
          break;
        case SVG_PRESERVEASPECTRATIO_XMINYMAX:
        case SVG_PRESERVEASPECTRATIO_XMIDYMAX:
        case SVG_PRESERVEASPECTRATIO_XMAXYMAX:
          f = aViewportHeight - a * aViewboxHeight;
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("Unknown value for align");
      }
    } else if ((meetOrSlice == SVG_MEETORSLICE_MEET && d < a) ||
               (meetOrSlice == SVG_MEETORSLICE_SLICE && a < d)) {
      a = d;
      switch (align) {
        case SVG_PRESERVEASPECTRATIO_XMINYMIN:
        case SVG_PRESERVEASPECTRATIO_XMINYMID:
        case SVG_PRESERVEASPECTRATIO_XMINYMAX:
          break;
        case SVG_PRESERVEASPECTRATIO_XMIDYMIN:
        case SVG_PRESERVEASPECTRATIO_XMIDYMID:
        case SVG_PRESERVEASPECTRATIO_XMIDYMAX:
          e = (aViewportWidth - a * aViewboxWidth) / 2.0f;
          break;
        case SVG_PRESERVEASPECTRATIO_XMAXYMIN:
        case SVG_PRESERVEASPECTRATIO_XMAXYMID:
        case SVG_PRESERVEASPECTRATIO_XMAXYMAX:
          e = aViewportWidth - a * aViewboxWidth;
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("Unknown value for align");
      }
    } else
      MOZ_ASSERT_UNREACHABLE("Unknown value for meetOrSlice");
  }

  if (aViewboxX) e += -a * aViewboxX;
  if (aViewboxY) f += -d * aViewboxY;

  return gfx::Matrix(a, 0.0f, 0.0f, d, e, f);
}

template <class floatType>
bool SVGContentUtils::ParseNumber(nsAString::const_iterator& aIter,
                                  const nsAString::const_iterator& aEnd,
                                  floatType& aValue) {
  const nsAString::const_iterator start(aIter);
  auto resetIterator = mozilla::MakeScopeExit([&]() { aIter = start; });
  int32_t sign;
  if (!SVGContentUtils::ParseOptionalSign(aIter, aEnd, sign)) {
    return false;
  }

  bool gotDot = *aIter == '.';

  if (!gotDot) {
    if (!mozilla::IsAsciiDigit(*aIter)) {
      return false;
    }
    do {
      ++aIter;
    } while (aIter != aEnd && mozilla::IsAsciiDigit(*aIter));

    if (aIter != aEnd) {
      gotDot = *aIter == '.';
    }
  }

  if (gotDot) {
    ++aIter;
    if (aIter == aEnd || !mozilla::IsAsciiDigit(*aIter)) {
      return false;
    }

    do {
      ++aIter;
    } while (aIter != aEnd && mozilla::IsAsciiDigit(*aIter));
  }

  bool gotE = false;

  if (aIter != aEnd && (*aIter == 'e' || *aIter == 'E')) {
    nsAString::const_iterator expIter(aIter);

    ++expIter;
    if (expIter != aEnd) {
      if (*expIter == '-' || *expIter == '+') {
        ++expIter;
      }
      if (expIter != aEnd && mozilla::IsAsciiDigit(*expIter)) {
        // At this point we're sure this is an exponent
        // and not the start of a unit such as em or ex.
        gotE = true;
      }
    }

    if (gotE) {
      aIter = expIter;
      do {
        ++aIter;
      } while (aIter != aEnd && mozilla::IsAsciiDigit(*aIter));
    }
  }

  resetIterator.release();
  return ::StringToValue(Substring(start, aIter), aValue);
}

template bool SVGContentUtils::ParseNumber<float>(
    nsAString::const_iterator& aIter, const nsAString::const_iterator& aEnd,
    float& aValue);
template bool SVGContentUtils::ParseNumber<double>(
    nsAString::const_iterator& aIter, const nsAString::const_iterator& aEnd,
    double& aValue);

template <class floatType>
bool SVGContentUtils::ParseNumber(const nsAString& aString, floatType& aValue) {
  nsAString::const_iterator iter, end;
  aString.BeginReading(iter);
  aString.EndReading(end);

  return ParseNumber(iter, end, aValue) && iter == end;
}

template bool SVGContentUtils::ParseNumber<float>(const nsAString& aString,
                                                  float& aValue);
template bool SVGContentUtils::ParseNumber<double>(const nsAString& aString,
                                                   double& aValue);

/* static */
bool SVGContentUtils::ParseInteger(nsAString::const_iterator& aIter,
                                   const nsAString::const_iterator& aEnd,
                                   int32_t& aValue) {
  nsAString::const_iterator iter(aIter);

  int32_t sign;
  if (!ParseOptionalSign(iter, aEnd, sign)) {
    return false;
  }

  if (!mozilla::IsAsciiDigit(*iter)) {
    return false;
  }

  int64_t value = 0;

  do {
    if (value <= std::numeric_limits<int32_t>::max()) {
      value = 10 * value + mozilla::AsciiAlphanumericToNumber(*iter);
    }
    ++iter;
  } while (iter != aEnd && mozilla::IsAsciiDigit(*iter));

  aIter = iter;
  aValue = int32_t(std::clamp(sign * value,
                              int64_t(std::numeric_limits<int32_t>::min()),
                              int64_t(std::numeric_limits<int32_t>::max())));
  return true;
}

/* static */
bool SVGContentUtils::ParseInteger(const nsAString& aString, int32_t& aValue) {
  nsAString::const_iterator iter, end;
  aString.BeginReading(iter);
  aString.EndReading(end);

  return ParseInteger(iter, end, aValue) && iter == end;
}

float SVGContentUtils::CoordToFloat(const SVGElement* aContent,
                                    const LengthPercentage& aLength,
                                    uint8_t aCtxType) {
  float result = aLength.ResolveToCSSPixelsWith([&] {
    SVGViewportElement* ctx = aContent->GetCtx();
    return CSSCoord(ctx ? ctx->GetLength(aCtxType) : 0.0f);
  });
  if (aLength.IsCalc()) {
    const auto& calc = aLength.AsCalc();
    if (calc.clamping_mode == StyleAllowedNumericType::NonNegative) {
      result = std::max(result, 0.0f);
    } else {
      MOZ_ASSERT(calc.clamping_mode == StyleAllowedNumericType::All);
    }
  }
  return result;
}

already_AddRefed<gfx::Path> SVGContentUtils::GetPath(
    const nsACString& aPathString) {
  SVGPathData pathData(aPathString);
  if (pathData.IsEmpty()) {
    return nullptr;
  }

  RefPtr<DrawTarget> drawTarget =
      gfxPlatform::ThreadLocalScreenReferenceDrawTarget();
  RefPtr<PathBuilder> builder =
      drawTarget->CreatePathBuilder(FillRule::FILL_WINDING);

  // This is called from canvas, so we don't need to get the effective zoom here
  // or so.
  return pathData.BuildPath(builder, StyleStrokeLinecap::Butt, 1, 1.0f);
}

bool SVGContentUtils::ShapeTypeHasNoCorners(const nsIContent* aContent) {
  return aContent &&
         aContent->IsAnyOfSVGElements(nsGkAtoms::circle, nsGkAtoms::ellipse);
}

nsDependentSubstring SVGContentUtils::GetAndEnsureOneToken(
    const nsAString& aString, bool& aSuccess) {
  nsWhitespaceTokenizerTemplate<nsContentUtils::IsHTMLWhitespace> tokenizer(
      aString);

  aSuccess = false;
  if (!tokenizer.hasMoreTokens()) {
    return {};
  }
  auto token = tokenizer.nextToken();
  if (tokenizer.hasMoreTokens()) {
    return {};
  }

  aSuccess = true;
  return token;
}

}  // namespace mozilla
