// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.compare
description: Various forms of time zone annotation; critical flag has no effect
features: [Temporal]
---*/

const tests = [
  ["1976-11-18T15:23[Asia/Kolkata]", "named, with no offset"],
  ["1976-11-18T15:23[!Europe/Vienna]", "named, with ! and no offset"],
  ["1976-11-18T15:23[+00:00]", "numeric, with no offset"],
  ["1976-11-18T15:23[!-02:30]", "numeric, with ! and no offset"],
  ["1976-11-18T15:23+00:00[UTC]", "named, with offset"],
  ["1976-11-18T15:23+00:00[!Africa/Abidjan]", "named, with offset and !"],
  ["1976-11-18T15:23+00:00[+01:00]", "numeric, with offset"],
  ["1976-11-18T15:23+00:00[!-08:00]", "numeric, with offset and !"],
];

tests.forEach(([arg, description]) => {
  const result = Temporal.PlainDateTime.compare(arg, arg);

  assert.sameValue(
    result,
    0,
    `time zone annotation (${description})`
  );
});

reportCompare(0, 0);
