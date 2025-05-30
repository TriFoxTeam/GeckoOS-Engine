// |reftest| skip-if(!Intl.hasOwnProperty('DurationFormat')) -- Intl.DurationFormat is not enabled unconditionally
// Copyright 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-Intl.DurationFormat.supportedLocalesOf
description: >
    Checks the "length" property of Intl.DurationFormat.supportedLocalesOf().
info: |
    The value of the length property of the supportedLocalesOf method is 1.
    Unless specified otherwise in this document, the objects, functions, and constructors described in this standard are subject to the generic requirements and restrictions specified for standard built-in ECMAScript objects in the ECMAScript 2019 Language Specification, 10th edition, clause 17, or successor.
    Every built-in function object, including constructors, has a length property whose value is an integer.
    Unless otherwise specified, the length property of a built-in function object has the attributes { [[Writable]]: false, [[Enumerable]]: false, [[Configurable]]: true }.
includes: [propertyHelper.js]
features: [Intl.DurationFormat]
---*/

verifyProperty(Intl.DurationFormat.supportedLocalesOf, "length", {
  value: 1,
  writable: false,
  enumerable: false,
  configurable: true,
});

reportCompare(0, 0);
