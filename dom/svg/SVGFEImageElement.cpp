/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGFEImageElement.h"

#include "mozilla/SVGObserverUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/SVGFEImageElementBinding.h"
#include "mozilla/dom/SVGFilterElement.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/RefPtr.h"
#include "nsContentUtils.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"
#include "imgIContainer.h"
#include "gfx2DGlue.h"

NS_IMPL_NS_NEW_SVG_ELEMENT(FEImage)

using namespace mozilla::gfx;

namespace mozilla::dom {

JSObject* SVGFEImageElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return SVGFEImageElement_Binding::Wrap(aCx, this, aGivenProto);
}

SVGElement::StringInfo SVGFEImageElement::sStringInfo[3] = {
    {nsGkAtoms::result, kNameSpaceID_None, true},
    {nsGkAtoms::href, kNameSpaceID_None, true},
    {nsGkAtoms::href, kNameSpaceID_XLink, true}};

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ISUPPORTS_INHERITED(SVGFEImageElement, SVGFEImageElementBase,
                            imgINotificationObserver, nsIImageLoadingContent)

//----------------------------------------------------------------------
// Implementation

SVGFEImageElement::SVGFEImageElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : SVGFEImageElementBase(std::move(aNodeInfo)), mImageAnimationMode(0) {
  // We start out broken
  AddStatesSilently(ElementState::BROKEN);
}

SVGFEImageElement::~SVGFEImageElement() { nsImageLoadingContent::Destroy(); }

//----------------------------------------------------------------------

nsresult SVGFEImageElement::LoadSVGImage(bool aForce, bool aNotify) {
  // resolve href attribute
  nsIURI* baseURI = GetBaseURI();

  nsAutoString href;
  if (mStringAttributes[HREF].IsExplicitlySet()) {
    mStringAttributes[HREF].GetAnimValue(href, this);
  } else {
    mStringAttributes[XLINK_HREF].GetAnimValue(href, this);
  }
  href.Trim(" \t\n\r");

  if (baseURI && !href.IsEmpty()) NS_MakeAbsoluteURI(href, href, baseURI);

  // Make sure we don't get in a recursive death-spiral
  Document* doc = OwnerDoc();
  nsCOMPtr<nsIURI> hrefAsURI;
  if (NS_SUCCEEDED(StringToURI(href, doc, getter_AddRefs(hrefAsURI)))) {
    bool isEqual;
    if (NS_SUCCEEDED(hrefAsURI->Equals(baseURI, &isEqual)) && isEqual) {
      // Image URI matches our URI exactly! Bail out.
      return NS_OK;
    }
  }

  // Mark channel as urgent-start before load image if the image load is
  // initaiated by a user interaction.
  mUseUrgentStartForChannel = UserActivation::IsHandlingUserInput();
  return LoadImage(href, aForce, aNotify, eImageLoadType_Normal);
}

bool SVGFEImageElement::ShouldLoadImage() const {
  return LoadingEnabled() && OwnerDoc()->ShouldLoadImages();
}

//----------------------------------------------------------------------
// EventTarget methods:

void SVGFEImageElement::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  nsImageLoadingContent::AsyncEventRunning(aEvent);
}

//----------------------------------------------------------------------
// nsIContent methods:

bool SVGFEImageElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                       const nsAString& aValue,
                                       nsIPrincipal* aMaybeScriptedPrincipal,
                                       nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::crossorigin) {
      ParseCORSValue(aValue, aResult);
      return true;
    }
    if (aAttribute == nsGkAtoms::fetchpriority) {
      ParseFetchPriority(aValue, aResult);
      return true;
    }
  }
  return SVGFEImageElementBase::ParseAttribute(
      aNamespaceID, aAttribute, aValue, aMaybeScriptedPrincipal, aResult);
}

void SVGFEImageElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aSubjectPrincipal,
                                     bool aNotify) {
  if (aName == nsGkAtoms::href && (aNamespaceID == kNameSpaceID_XLink ||
                                   aNamespaceID == kNameSpaceID_None)) {
    if (aNamespaceID == kNameSpaceID_XLink &&
        mStringAttributes[HREF].IsExplicitlySet()) {
      // href overrides xlink:href
      return;
    }
    if (aValue || (aNamespaceID == kNameSpaceID_None &&
                   mStringAttributes[XLINK_HREF].IsExplicitlySet())) {
      if (ShouldLoadImage()) {
        LoadSVGImage(true, aNotify);
      }
    } else {
      CancelImageRequests(aNotify);
    }
  } else if (aNamespaceID == kNameSpaceID_None &&
             aName == nsGkAtoms::crossorigin) {
    if (aNotify && GetCORSMode() != AttrValueToCORSMode(aOldValue) &&
        ShouldLoadImage()) {
      ForceReload(aNotify, IgnoreErrors());
    }
  }

  return SVGFEImageElementBase::AfterSetAttr(
      aNamespaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

void SVGFEImageElement::MaybeLoadSVGImage() {
  if ((mStringAttributes[HREF].IsExplicitlySet() ||
       mStringAttributes[XLINK_HREF].IsExplicitlySet()) &&
      (NS_FAILED(LoadSVGImage(false, true)) || !LoadingEnabled())) {
    CancelImageRequests(true);
  }
}

nsresult SVGFEImageElement::BindToTree(BindContext& aContext,
                                       nsINode& aParent) {
  nsresult rv = SVGFEImageElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  nsImageLoadingContent::BindToTree(aContext, aParent);

  if ((mStringAttributes[HREF].IsExplicitlySet() ||
       mStringAttributes[XLINK_HREF].IsExplicitlySet()) &&
      ShouldLoadImage()) {
    nsContentUtils::AddScriptRunner(
        NewRunnableMethod("dom::SVGFEImageElement::MaybeLoadSVGImage", this,
                          &SVGFEImageElement::MaybeLoadSVGImage));
  }

  if (aContext.InComposedDoc()) {
    aContext.OwnerDoc().SetUseCounter(eUseCounter_custom_feImage);
  }

  return rv;
}

void SVGFEImageElement::UnbindFromTree(UnbindContext& aContext) {
  nsImageLoadingContent::UnbindFromTree();
  SVGFEImageElementBase::UnbindFromTree(aContext);
}

void SVGFEImageElement::DestroyContent() {
  nsImageLoadingContent::Destroy();
  SVGFEImageElementBase::DestroyContent();
}

//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGFEImageElement)

already_AddRefed<DOMSVGAnimatedString> SVGFEImageElement::Href() {
  return mStringAttributes[HREF].IsExplicitlySet()
             ? mStringAttributes[HREF].ToDOMAnimatedString(this)
             : mStringAttributes[XLINK_HREF].ToDOMAnimatedString(this);
}

//----------------------------------------------------------------------
//  nsImageLoadingContent methods:

CORSMode SVGFEImageElement::GetCORSMode() {
  return AttrValueToCORSMode(GetParsedAttr(nsGkAtoms::crossorigin));
}

void SVGFEImageElement::GetFetchPriority(nsAString& aFetchPriority) const {
  GetEnumAttr(nsGkAtoms::fetchpriority, kFetchPriorityAttributeValueAuto,
              aFetchPriority);
}

//----------------------------------------------------------------------
// nsIDOMSVGFEImageElement methods

FilterPrimitiveDescription SVGFEImageElement::GetPrimitiveDescription(
    SVGFilterInstance* aInstance, const IntRect& aFilterSubregion,
    const nsTArray<bool>& aInputsAreTainted,
    nsTArray<RefPtr<SourceSurface>>& aInputImages) {
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return FilterPrimitiveDescription();
  }

  nsCOMPtr<imgIRequest> currentRequest;
  GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
             getter_AddRefs(currentRequest));

  nsCOMPtr<imgIContainer> imageContainer;
  if (currentRequest) {
    currentRequest->GetImage(getter_AddRefs(imageContainer));
  }

  RefPtr<SourceSurface> image;
  nsIntSize nativeSize;
  if (imageContainer) {
    if (NS_FAILED(imageContainer->GetWidth(&nativeSize.width))) {
      nativeSize.width = kFallbackIntrinsicWidthInPixels;
    }
    if (NS_FAILED(imageContainer->GetHeight(&nativeSize.height))) {
      nativeSize.height = kFallbackIntrinsicHeightInPixels;
    }
    uint32_t flags =
        imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY;
    image = imageContainer->GetFrameAtSize(nativeSize,
                                           imgIContainer::FRAME_CURRENT, flags);
  }

  if (!image) {
    return FilterPrimitiveDescription();
  }

  Matrix viewBoxTM = SVGContentUtils::GetViewBoxTransform(
      aFilterSubregion.width, aFilterSubregion.height, 0, 0, nativeSize.width,
      nativeSize.height, mPreserveAspectRatio);
  Matrix TM = viewBoxTM;
  TM.PostTranslate(aFilterSubregion.x, aFilterSubregion.y);

  SamplingFilter samplingFilter =
      nsLayoutUtils::GetSamplingFilterForFrame(frame);

  ImageAttributes atts;
  atts.mFilter = (uint32_t)samplingFilter;
  atts.mTransform = TM;

  // Append the image to aInputImages and store its index in the description.
  size_t imageIndex = aInputImages.Length();
  aInputImages.AppendElement(image);
  atts.mInputIndex = (uint32_t)imageIndex;
  return FilterPrimitiveDescription(AsVariant(std::move(atts)));
}

bool SVGFEImageElement::AttributeAffectsRendering(int32_t aNameSpaceID,
                                                  nsAtom* aAttribute) const {
  // nsGkAtoms::href is deliberately omitted as the frame has special
  // handling to load the image
  return SVGFEImageElementBase::AttributeAffectsRendering(aNameSpaceID,
                                                          aAttribute) ||
         (aNameSpaceID == kNameSpaceID_None &&
          aAttribute == nsGkAtoms::preserveAspectRatio);
}

bool SVGFEImageElement::OutputIsTainted(const nsTArray<bool>& aInputsAreTainted,
                                        nsIPrincipal* aReferencePrincipal) {
  nsresult rv;
  nsCOMPtr<imgIRequest> currentRequest;
  GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
             getter_AddRefs(currentRequest));

  if (!currentRequest) {
    return false;
  }

  nsCOMPtr<nsIPrincipal> principal;
  rv = currentRequest->GetImagePrincipal(getter_AddRefs(principal));
  if (NS_FAILED(rv) || !principal) {
    return true;
  }

  // If CORS was used to load the image, the page is allowed to read from it.
  if (nsLayoutUtils::ImageRequestUsesCORS(currentRequest)) {
    return false;
  }

  if (aReferencePrincipal->Subsumes(principal)) {
    // The page is allowed to read from the image.
    return false;
  }

  return true;
}

//----------------------------------------------------------------------
// SVGElement methods

already_AddRefed<DOMSVGAnimatedPreserveAspectRatio>
SVGFEImageElement::PreserveAspectRatio() {
  return mPreserveAspectRatio.ToDOMAnimatedPreserveAspectRatio(this);
}

SVGAnimatedPreserveAspectRatio*
SVGFEImageElement::GetAnimatedPreserveAspectRatio() {
  return &mPreserveAspectRatio;
}

SVGElement::StringAttributesInfo SVGFEImageElement::GetStringInfo() {
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              std::size(sStringInfo));
}

//----------------------------------------------------------------------
// nsIImageLoadingContent methods
NS_IMETHODIMP_(void)
SVGFEImageElement::FrameCreated(nsIFrame* aFrame) {
  nsImageLoadingContent::FrameCreated(aFrame);

  uint64_t mode = aFrame->PresContext()->ImageAnimationMode();
  if (mode == mImageAnimationMode) {
    return;
  }

  mImageAnimationMode = mode;

  if (mPendingRequest) {
    nsCOMPtr<imgIContainer> container;
    mPendingRequest->GetImage(getter_AddRefs(container));
    if (container) {
      container->SetAnimationMode(mode);
    }
  }

  if (mCurrentRequest) {
    nsCOMPtr<imgIContainer> container;
    mCurrentRequest->GetImage(getter_AddRefs(container));
    if (container) {
      container->SetAnimationMode(mode);
    }
  }
}

//----------------------------------------------------------------------
// imgINotificationObserver methods

void SVGFEImageElement::Notify(imgIRequest* aRequest, int32_t aType,
                               const nsIntRect* aData) {
  nsImageLoadingContent::Notify(aRequest, aType, aData);

  if (aType == imgINotificationObserver::SIZE_AVAILABLE) {
    // Request a decode
    nsCOMPtr<imgIContainer> container;
    aRequest->GetImage(getter_AddRefs(container));
    MOZ_ASSERT(container, "who sent the notification then?");
    container->StartDecoding(imgIContainer::FLAG_NONE);
    container->SetAnimationMode(mImageAnimationMode);
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE ||
      aType == imgINotificationObserver::FRAME_UPDATE ||
      aType == imgINotificationObserver::SIZE_AVAILABLE) {
    if (auto* filter = SVGFilterElement::FromNodeOrNull(GetParent())) {
      SVGObserverUtils::InvalidateDirectRenderingObservers(filter);
    }
  }
}

void SVGFEImageElement::DidAnimateAttribute(int32_t aNameSpaceID,
                                            nsAtom* aAttribute) {
  if ((aNameSpaceID == kNameSpaceID_None ||
       aNameSpaceID == kNameSpaceID_XLink) &&
      aAttribute == nsGkAtoms::href) {
    bool hrefIsSet =
        mStringAttributes[SVGFEImageElement::HREF].IsExplicitlySet() ||
        mStringAttributes[SVGFEImageElement::XLINK_HREF].IsExplicitlySet();
    if (hrefIsSet) {
      LoadSVGImage(true, true);
    } else {
      CancelImageRequests(true);
    }
  }
  SVGFEImageElementBase::DidAnimateAttribute(aNameSpaceID, aAttribute);
}

}  // namespace mozilla::dom
