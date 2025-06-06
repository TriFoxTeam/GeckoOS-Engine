/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  formatDisplayName,
} = require("resource://devtools/server/actors/frame.js");
const {
  TYPES,
  getResourceWatcher,
} = require("resource://devtools/server/actors/resources/index.js");

// Get a string message to display when a frame evaluation throws.
function getThrownMessage(completion) {
  try {
    if (completion.throw.getOwnPropertyDescriptor) {
      return completion.throw.getOwnPropertyDescriptor("message").value;
    } else if (completion.toString) {
      return completion.toString();
    }
  } catch (ex) {
    // ignore
  }
  return "Unknown exception";
}
module.exports.getThrownMessage = getThrownMessage;

function evalAndLogEvent({ threadActor, frame, level, expression, bindings }) {
  const frameLocation = threadActor.sourcesManager.getFrameLocation(frame);
  const { sourceActor, line } = frameLocation;
  const displayName = formatDisplayName(frame);

  // TODO remove this branch when (#1749668) lands (#1609540)
  if (isWorker) {
    threadActor.targetActor._consoleActor.evaluateJS({
      text: `console.log(...${expression})`,
      bindings: { displayName, ...bindings },
      url: sourceActor.url,
      lineNumber: line,
      disableBreaks: true,
    });

    return undefined;
  }

  let completion;
  // Ensure disabling all types of breakpoints for all sources while evaluating the log points
  threadActor.insideClientEvaluation = { disableBreaks: true };
  try {
    completion = frame.evalWithBindings(
      expression,
      {
        displayName,
        ...bindings,
      },
      { hideFromDebugger: true }
    );
  } finally {
    threadActor.insideClientEvaluation = null;
  }

  let value;
  if (!completion) {
    // The evaluation was killed (possibly by the slow script dialog).
    value = ["Evaluation failed"];
  } else if ("return" in completion) {
    value = completion.return;
  } else {
    value = [getThrownMessage(completion)];
    level = `${level}Error`;
  }

  if (value && typeof value.unsafeDereference === "function") {
    value = value.unsafeDereference();
  }

  ChromeUtils.addProfilerMarker("Debugger log point", undefined, value);

  emitConsoleMessage(threadActor, frameLocation, value, level);

  return undefined;
}

function logEvent({ threadActor, frame }) {
  const frameLocation = threadActor.sourcesManager.getFrameLocation(frame);
  const { sourceActor, line } = frameLocation;

  // TODO remove this branch when (#1749668) lands (#1609540)
  if (isWorker) {
    const bindings = {};
    for (let i = 0; i < frame.arguments.length; i++) {
      bindings[`x${i}`] = frame.arguments[i];
    }
    threadActor.targetActor._consoleActor.evaluateJS({
      text: `console.log(${Object.keys(bindings).join(",")})`,
      bindings,
      url: sourceActor.url,
      lineNumber: line,
      disableBreaks: true,
    });

    return undefined;
  }

  const args = [];
  for (const arg of frame.arguments) {
    args.push(
      arg && typeof arg.unsafeDereference === "function"
        ? arg.unsafeDereference()
        : arg
    );
  }

  emitConsoleMessage(threadActor, frameLocation, args, "logPoint");

  return undefined;
}

function emitConsoleMessage(threadActor, frameLocation, args, level) {
  const targetActor = threadActor.targetActor;
  const { sourceActor, line, column } = frameLocation;

  const message = {
    filename: sourceActor.url,
    lineNumber: line,
    columnNumber: column,
    arguments: args,
    level,
    timeStamp: ChromeUtils.dateNow(),
    chromeContext:
      targetActor.actorID &&
      /conn\d+\.parentProcessTarget\d+/.test(targetActor.actorID),
    // The 'prepareConsoleMessageForRemote' method in webconsoleActor expects internal source ID,
    // thus we can't set sourceId directly to sourceActorID.
    sourceId: sourceActor.internalSourceId,
  };

  // Note that only WindowGlobalTarget actor support resource watcher
  // This is still missing for worker and content processes
  const consoleMessageWatcher = getResourceWatcher(
    targetActor,
    TYPES.CONSOLE_MESSAGE
  );
  if (consoleMessageWatcher) {
    consoleMessageWatcher.emitMessages([message]);
  } else {
    // Bug 1642296: Once we enable ConsoleMessage resource on the server, we should remove onConsoleAPICall
    // from the WebConsoleActor, and only support the ConsoleMessageWatcher codepath.
    targetActor._consoleActor.onConsoleAPICall(message);
  }
}

module.exports.evalAndLogEvent = evalAndLogEvent;
module.exports.logEvent = logEvent;
