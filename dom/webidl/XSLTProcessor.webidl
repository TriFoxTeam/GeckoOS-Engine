/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

typedef (unrestricted double or boolean or DOMString or Node or sequence<Node> or XPathResult) XSLTParameterValue;

[Exposed=Window]
interface XSLTProcessor {
    [UseCounter]
    constructor();

    /**
     * Import the stylesheet into this XSLTProcessor for transformations.
     *
     * @param style The root-node of a XSLT stylesheet. This can be either
     *              a document node or an element node. If a document node
     *              then the document can contain either a XSLT stylesheet
     *              or a LRE stylesheet.
     *              If the argument is an element node it must be the
     *              xsl:stylesheet (or xsl:transform) element of an XSLT
     *              stylesheet.
     */
    [Throws]
    undefined importStylesheet(Node style);

    /**
     * Transforms the node source applying the stylesheet given by
     * the importStylesheet() function. The owner document of the output node
     * owns the returned document fragment.
     *
     * @param source The node to be transformed
     * @param output This document is used to generate the output
     * @return DocumentFragment The result of the transformation
     */
    [CEReactions, Throws]
    DocumentFragment transformToFragment(Node source,
                                         Document output);

    /**
     * Transforms the node source applying the stylesheet given by the
     * importStylesheet() function.
     *
     * @param source The node to be transformed
     * @return Document The result of the transformation
     */
    [CEReactions, Throws]
    Document transformToDocument(Node source);

    /**
     * Sets a parameter to be used in subsequent transformations with this
     * XSLTProcessor. If the parameter doesn't exist in the stylesheet the
     * parameter will be ignored.
     *
     * @param namespaceURI The namespaceURI of the XSLT parameter
     * @param localName    The local name of the XSLT parameter
     * @param value        The new value of the XSLT parameter
     */
    [Throws]
    undefined setParameter([LegacyNullToEmptyString] DOMString namespaceURI,
                           DOMString localName,
                           XSLTParameterValue value);

    /**
     * Gets a parameter if previously set by setParameter. Returns null
     * otherwise.
     *
     * @param namespaceURI    The namespaceURI of the XSLT parameter
     * @param localName       The local name of the XSLT parameter
     * @return ParameterValue The value of the XSLT parameter
     */
    [Throws]
    XSLTParameterValue? getParameter([LegacyNullToEmptyString] DOMString namespaceURI,
                                     DOMString localName);
    /**
     * Removes a parameter, if set. This will make the processor use the
     * default-value for the parameter as specified in the stylesheet.
     *
     * @param namespaceURI The namespaceURI of the XSLT parameter
     * @param localName    The local name of the XSLT parameter
     */
    [Throws]
    undefined removeParameter([LegacyNullToEmptyString] DOMString namespaceURI,
                              DOMString localName);

    /**
     * Removes all set parameters from this XSLTProcessor. This will make
     * the processor use the default-value for all parameters as specified in
     * the stylesheet.
     */
    [Throws]
    undefined clearParameters();

    /**
     * Remove all parameters and stylesheets from this XSLTProcessor.
     */
    [Throws]
    undefined reset();

    /**
    * Disables all loading of external documents, such as from
    * <xsl:import> and document()
    * Defaults to off and is *not* reset by calls to reset()
    */
    [ChromeOnly]
    const unsigned long DISABLE_ALL_LOADS = 1;

    /**
    * Flags for this processor. Defaults to 0. See individual flags above
    * for documentation for effect of reset()
    */
    [ChromeOnly, NeedsCallerType]
    attribute unsigned long flags;
};
