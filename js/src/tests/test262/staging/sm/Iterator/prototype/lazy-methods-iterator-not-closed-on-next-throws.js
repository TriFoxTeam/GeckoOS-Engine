// |reftest| shell-option(--enable-iterator-helpers) skip-if(!this.hasOwnProperty('Iterator')||!xulRuntime.shell) -- iterator-helpers is not enabled unconditionally, requires shell-options
// Copyright (C) 2024 Mozilla Corporation. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: pending
description: |
  Lazy %Iterator.prototype% methods don't close the iterator if `.next` call throws.
info: |
  Iterator Helpers proposal 2.1.5
features:
  - iterator-helpers
includes: [sm/non262.js, sm/non262-shell.js]
flags:
  - noStrict
---*/

//
//
class TestError extends Error {}
class TestIterator extends Iterator {
  next() {
    throw new TestError();
  }

  closed = false;
  return() {
    this.closed = true;
    return {done: true};
  }
}

const iterator = new TestIterator();

const methods = [
  iter => iter.map(x => x),
  iter => iter.filter(x => x),
  iter => iter.take(1),
  iter => iter.drop(0),
  iter => iter.flatMap(x => [x]),
];

for (const method of methods) {
  assert.sameValue(iterator.closed, false);
  assertThrowsInstanceOf(() => method(iterator).next(), TestError);
  assert.sameValue(iterator.closed, false);
}


reportCompare(0, 0);
