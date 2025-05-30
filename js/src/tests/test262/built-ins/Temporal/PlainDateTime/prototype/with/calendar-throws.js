// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Throws if a calendar is supplied
esid: sec-temporal.plaindatetime.prototype.with
features: [Temporal]
---*/

const datetime = new Temporal.PlainDateTime(1976, 11, 18, 15, 23, 30, 123, 456, 789);

assert.throws(
  TypeError,
  () => datetime.with({ year: 2021, calendar: "iso8601" }),
  "throws with calendar property"
);

reportCompare(0, 0);
