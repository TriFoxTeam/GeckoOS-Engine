// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.instant.prototype.equals
description: Leap second is a valid ISO string for Instant
features: [Temporal]
---*/

const instance = new Temporal.Instant(1_483_228_799_000_000_000n);

const arg = "2016-12-31T23:59:60Z";
const result = instance.equals(arg);
assert.sameValue(
  result,
  true,
  "leap second is a valid ISO string for Instant"
);

reportCompare(0, 0);
