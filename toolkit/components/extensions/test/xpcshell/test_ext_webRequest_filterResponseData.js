"use strict";

const HOSTS = new Set(["example.com", "example.org", "example.net"]);

const server = createHttpServer({ hosts: HOSTS });

const FETCH_ORIGIN = "http://example.com/dummy";

server.registerDirectory("/data/", do_get_file("data"));

server.registerPathHandler("/redirect", (request, response) => {
  let params = new URLSearchParams(request.queryString);
  response.setStatusLine(request.httpVersion, 302, "Moved Temporarily");
  response.setHeader("Location", params.get("redirect_uri"));
  response.setHeader("Access-Control-Allow-Origin", "*");
});

server.registerPathHandler("/redirect301", (request, response) => {
  let params = new URLSearchParams(request.queryString);
  response.setStatusLine(request.httpVersion, 301, "Moved Permanently");
  response.setHeader("Location", params.get("redirect_uri"));
  response.setHeader("Access-Control-Allow-Origin", "*");
});

server.registerPathHandler("/script302.js", (request, response) => {
  response.setStatusLine(request.httpVersion, 302, "Moved Temporarily");
  response.setHeader("Location", "http://example.com/script.js");
});

server.registerPathHandler("/script.js", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "application/javascript");
  response.write(String.raw`console.log("HELLO!");`);
});

server.registerPathHandler("/302.html", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/html");
  response.write(String.raw`<script src="/script302.js"></script>`);
});

server.registerPathHandler("/dummy", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Access-Control-Allow-Origin", "*");
  response.write("ok");
});

server.registerPathHandler("/dummy.xhtml", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "application/xhtml+xml");
  response.write(String.raw`<?xml version="1.0"?>
    <html xml:lang="en" xmlns="http://www.w3.org/1999/xhtml">
      <head/>
      <body/>
    </html>
  `);
});

server.registerPathHandler("/lorem.html.gz", async (request, response) => {
  response.processAsync();

  response.setHeader(
    "Content-Type",
    "Content-Type: text/html; charset=utf-8",
    false
  );
  response.setHeader("Content-Encoding", "gzip", false);

  let data = await IOUtils.read(do_get_file("data/lorem.html.gz").path);
  response.write(String.fromCharCode(...data));

  response.finish();
});

// Test re-encoding the data stream for bug 1590898.
add_task(async function test_stream_encoding_data() {
  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.webRequest.onBeforeRequest.addListener(
        request => {
          let filter = browser.webRequest.filterResponseData(request.requestId);
          let decoder = new TextDecoder("utf-8");
          let encoder = new TextEncoder();

          filter.ondata = event => {
            let str = decoder.decode(event.data, { stream: true });
            filter.write(encoder.encode(str));
            filter.disconnect();
          };
        },
        {
          urls: ["http://example.com/lorem.html.gz"],
          types: ["main_frame"],
        },
        ["blocking"]
      );
    },

    manifest: {
      permissions: ["webRequest", "webRequestBlocking", "http://example.com/"],
    },
  });

  await extension.startup();

  let contentPage = await ExtensionTestUtils.loadContentPage(
    "http://example.com/lorem.html.gz"
  );

  let content = await contentPage.spawn([], () => {
    return this.content.document.body.textContent;
  });

  ok(
    content.includes("Lorem ipsum dolor sit amet"),
    `expected content received`
  );

  await contentPage.close();
  await extension.unload();
});

// Tests that the stream filter request is added to the document's load
// group, and blocks an XML document's load event until after the filter
// stops sending data.
add_task(async function test_xml_document_loadgroup_blocking() {
  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.webRequest.onBeforeRequest.addListener(
        request => {
          let filter = browser.webRequest.filterResponseData(request.requestId);

          let data = [];
          filter.ondata = event => {
            data.push(event.data);
          };
          filter.onstop = async () => {
            browser.test.sendMessage("phase", "original-onstop");

            // Make a few trips through the event loop.
            for (let i = 0; i < 10; i++) {
              await new Promise(resolve => setTimeout(resolve, 0));
            }

            for (let buffer of data) {
              filter.write(buffer);
            }
            browser.test.sendMessage("phase", "filter-onstop");
            filter.close();
          };
        },
        {
          urls: ["http://example.com/dummy.xhtml"],
        },
        ["blocking"]
      );
    },

    files: {
      "content_script.js"() {
        browser.test.sendMessage("phase", "content-script-start");
        window.addEventListener(
          "DOMContentLoaded",
          () => {
            browser.test.sendMessage("phase", "content-script-domload");
          },
          { once: true }
        );
        window.addEventListener(
          "load",
          () => {
            browser.test.sendMessage("phase", "content-script-load");
          },
          { once: true }
        );
      },
    },

    manifest: {
      permissions: ["webRequest", "webRequestBlocking", "http://example.com/"],

      content_scripts: [
        {
          matches: ["http://example.com/dummy.xhtml"],
          run_at: "document_start",
          js: ["content_script.js"],
        },
      ],
    },
  });

  await extension.startup();

  const EXPECTED = [
    "original-onstop",
    "filter-onstop",
    "content-script-start",
    "content-script-domload",
    "content-script-load",
  ];

  let done = new Promise(resolve => {
    let phases = [];
    extension.onMessage("phase", phase => {
      phases.push(phase);
      if (phases.length === EXPECTED.length) {
        resolve(phases);
      }
    });
  });

  let contentPage = await ExtensionTestUtils.loadContentPage(
    "http://example.com/dummy.xhtml"
  );

  deepEqual(await done, EXPECTED, "Things happened, and in the right order");

  await contentPage.close();
  await extension.unload();
});

add_task(async function test_filter_content_fetch() {
  let extension = ExtensionTestUtils.loadExtension({
    background() {
      let pending = [];

      browser.webRequest.onBeforeRequest.addListener(
        data => {
          let filter = browser.webRequest.filterResponseData(data.requestId);

          let url = new URL(data.url);

          if (url.searchParams.get("redirect_uri")) {
            pending.push(
              new Promise(resolve => {
                filter.onerror = resolve;
              }).then(() => {
                browser.test.assertEq(
                  "Channel redirected",
                  filter.error,
                  "Got correct error for redirected filter"
                );
              })
            );
          }

          filter.onstart = () => {
            filter.write(new TextEncoder().encode(data.url));
          };
          filter.ondata = event => {
            let str = new TextDecoder().decode(event.data);
            browser.test.assertEq(
              "ok",
              str,
              `Got unfiltered data for ${data.url}`
            );
          };
          filter.onstop = () => {
            filter.close();
          };
        },
        {
          urls: ["<all_urls>"],
        },
        ["blocking"]
      );

      browser.test.onMessage.addListener(async msg => {
        if (msg === "done") {
          await Promise.all(pending);
          browser.test.notifyPass("stream-filter");
        }
      });
    },

    manifest: {
      permissions: [
        "webRequest",
        "webRequestBlocking",
        "http://example.com/",
        "http://example.org/",
      ],
    },
  });

  await extension.startup();

  let results = [
    ["http://example.com/dummy", "http://example.com/dummy"],
    ["http://example.org/dummy", "http://example.org/dummy"],
    ["http://example.net/dummy", "ok"],
    [
      "http://example.com/redirect?redirect_uri=http://example.com/dummy",
      "http://example.com/dummy",
    ],
    [
      "http://example.com/redirect?redirect_uri=http://example.org/dummy",
      "http://example.org/dummy",
    ],
    ["http://example.com/redirect?redirect_uri=http://example.net/dummy", "ok"],
    [
      "http://example.net/redirect?redirect_uri=http://example.com/dummy",
      "http://example.com/dummy",
    ],
  ].map(async ([url, expectedResponse]) => {
    let text = await ExtensionTestUtils.fetch(FETCH_ORIGIN, url);
    equal(text, expectedResponse, `Expected response for ${url}`);
  });

  await Promise.all(results);

  extension.sendMessage("done");
  await extension.awaitFinish("stream-filter");
  await extension.unload();
});

add_task(async function test_filter_301() {
  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.webRequest.onHeadersReceived.addListener(
        data => {
          if (data.statusCode !== 200) {
            return;
          }
          let filter = browser.webRequest.filterResponseData(data.requestId);

          filter.onstop = () => {
            filter.close();
            browser.test.notifyPass("stream-filter");
          };
          filter.onerror = () => {
            browser.test.fail(`unexpected ${filter.error}`);
          };
        },
        {
          urls: ["<all_urls>"],
        },
        ["blocking"]
      );
    },

    manifest: {
      permissions: [
        "webRequest",
        "webRequestBlocking",
        "http://example.com/",
        "http://example.org/",
      ],
    },
  });

  await extension.startup();

  let contentPage = await ExtensionTestUtils.loadContentPage(
    "http://example.com/redirect301?redirect_uri=http://example.org/dummy"
  );

  await extension.awaitFinish("stream-filter");

  await contentPage.close();
  await extension.unload();
});

add_task(async function test_filter_302() {
  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.webRequest.onBeforeRequest.addListener(
        details => {
          let filename = details.url.split("/").pop();
          let filter = browser.webRequest.filterResponseData(details.requestId);
          browser.test.sendMessage("filter-created");

          filter.ondata = () => {
            const script = "forceError();";
            filter.write(new Uint8Array(new TextEncoder().encode(script)));
            filter.close();
            browser.test.assertEq("script.js", filename, "Is redirect target");
            browser.test.sendMessage("filter-ondata");
          };

          filter.onerror = () => {
            browser.test.assertEq(filter.error, "Channel redirected");
            browser.test.assertEq("script302.js", filename, "Is initial file");
            browser.test.sendMessage("filter-redirect");
          };
        },
        {
          urls: ["http://example.com/*.js"],
        },
        ["blocking"]
      );
    },

    manifest: {
      permissions: ["webRequest", "webRequestBlocking", "http://example.com/"],
    },
  });

  await extension.startup();

  let { messages } = await promiseConsoleOutput(async () => {
    let contentPage = await ExtensionTestUtils.loadContentPage(
      "http://example.com/302.html"
    );

    await extension.awaitMessage("filter-created");
    await extension.awaitMessage("filter-redirect");
    await extension.awaitMessage("filter-created");
    await extension.awaitMessage("filter-ondata");
    await contentPage.close();
  });
  AddonTestUtils.checkMessages(messages, {
    expected: [{ message: /forceError is not defined/ }],
  });

  await extension.unload();
});

async function do_test_filter_302_cross_origin(withFullPermissions) {
  const HOST_PERMISSIONS_EXCLUDE_TARGET = [
    "http://example.net/", // Origin of initiator and pre-redirect URL.
  ];
  const HOST_PERMISSIONS_INCLUDE_TARGET = [
    "http://example.net/", // Origin of initiator and pre-redirect URL.
    "http://example.com/", // Origin of redirect target.
  ];

  let host_permissions = withFullPermissions
    ? HOST_PERMISSIONS_INCLUDE_TARGET
    : HOST_PERMISSIONS_EXCLUDE_TARGET;
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["webRequest", "webRequestBlocking", ...host_permissions],
    },
    background() {
      let requestId;
      function checkStreamFilter(hasRedirected) {
        let prefix = hasRedirected ? "AFTER: " : "BEFORE: ";
        let filter = browser.webRequest.filterResponseData(requestId);
        filter.onstart = () => {
          const script = "forceAfterRedirError();";
          filter.write(new Uint8Array(new TextEncoder().encode(script)));
          filter.disconnect();
          browser.test.sendMessage("filter-result", prefix + "onstart_fired");
        };
        filter.onerror = () => {
          browser.test.sendMessage("filter-result", prefix + filter.error);
        };
      }
      browser.test.onMessage.addListener((msg, otherRequestId) => {
        // The test calls us when the request has redirected.
        browser.test.assertEq("checkStreamFilter", msg, "expected msg");
        browser.test.assertEq(requestId, otherRequestId, "Same requestId");
        checkStreamFilter(/* hasRedirected */ true);
        browser.test.sendMessage("checkStreamFilter_initialized");
      });
      browser.webRequest.onBeforeRequest.addListener(
        details => {
          requestId = details.requestId;
          browser.test.log(`Seen request ${details.url} (${requestId})`);
          checkStreamFilter(/* hasRedirected */ false);
          browser.test.sendMessage("seenRequest");
        },
        { urls: ["http://example.net/script302.js"] },
        ["blocking"]
      );
    },
  });

  // Helper extension is used to detect the post-redirect request.
  const helperExtension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: [
        "webRequest",
        "webRequestBlocking",
        ...HOST_PERMISSIONS_INCLUDE_TARGET,
      ],
    },
    background() {
      const { promise, resolve } = Promise.withResolvers();
      browser.test.onMessage.addListener(msg => {
        browser.test.assertEq("continueRedirect", msg, "Got continueRedirect");
        resolve();
      });
      browser.webRequest.onBeforeRequest.addListener(
        async details => {
          browser.test.sendMessage("detectedRedirect", details.requestId);
          browser.test.log(`Suspending request to ${details.url}`);
          await promise;
        },
        { urls: ["http://example.com/script.js"] },
        ["blocking"]
      );
    },
  });

  await extension.startup();
  await helperExtension.startup();

  let { messages } = await promiseConsoleOutput(async () => {
    // Loads example.net/script302.js which redirects to example.com/script.js.
    // helperExtension suspends the script request, which in turn prevents the
    // HTML page from completing its load. We therefore don't await the
    // loadContentPage promise here.
    let contentPagePromise = ExtensionTestUtils.loadContentPage(
      "http://example.net/302.html"
    );

    await extension.awaitMessage("seenRequest");

    const requestId = await helperExtension.awaitMessage("detectedRedirect");

    info(`Detected and suspended redirect request with ID ${requestId}`);

    extension.sendMessage("checkStreamFilter", requestId);
    await extension.awaitMessage("checkStreamFilter_initialized");

    if (!withFullPermissions) {
      // StreamFilter is usually processed after OnStartRequest, except when the
      // extension does not have access to the channel - when the permission
      // check fails, the StreamFilter creation request is immediately denied.
      equal(
        await extension.awaitMessage("filter-result"),
        "AFTER: Invalid request ID",
        "Without permission, filter is not created after redirect"
      );
    }

    // Note: StreamFilter processing starts at OnStartRequest, after the
    // onHeadersReceived stage. So we need to resume the request before we can
    // receive any StreamFilter events.
    helperExtension.sendMessage("continueRedirect");

    if (withFullPermissions) {
      equal(
        await extension.awaitMessage("filter-result"),
        "BEFORE: Channel redirected",
        "With permission, pre-redirect filter is disconnected on redirect"
      );
      equal(
        await extension.awaitMessage("filter-result"),
        "AFTER: onstart_fired",
        "With permission, filter registered after redirect is connected"
      );
    } else {
      equal(
        await extension.awaitMessage("filter-result"),
        "BEFORE: Channel redirected",
        "Without permission, pre-redirect filter is disconnected on redirect"
      );
    }

    let contentPage = await contentPagePromise;
    await contentPage.close();
  });
  if (withFullPermissions) {
    AddonTestUtils.checkMessages(messages, {
      expected: [{ message: /forceAfterRedirError is not defined/ }],
    });
  } else {
    AddonTestUtils.checkMessages(messages, {
      forbidden: [{ message: /forceAfterRedirError is not defined/ }],
    });
  }

  await helperExtension.unload();
  await extension.unload();
}

add_task(async function test_filter_302_cross_origin_and_full_permissions() {
  await do_test_filter_302_cross_origin(/* withFullPermissions */ true);
});

add_task(async function test_filter_302_cross_origin_and_missing_permissions() {
  await do_test_filter_302_cross_origin(/* withFullPermissions */ false);
});

add_task(async function test_alternate_cached_data() {
  Services.prefs.setBoolPref("dom.script_loader.bytecode_cache.enabled", true);
  Services.prefs.setIntPref("dom.script_loader.bytecode_cache.strategy", -1);

  let extension = ExtensionTestUtils.loadExtension({
    background() {
      browser.webRequest.onBeforeRequest.addListener(
        details => {
          let filter = browser.webRequest.filterResponseData(details.requestId);
          let decoder = new TextDecoder("utf-8");
          let encoder = new TextEncoder();

          filter.ondata = event => {
            let str = decoder.decode(event.data, { stream: true });
            filter.write(encoder.encode(str));
            filter.disconnect();
            browser.test.assertTrue(
              str.startsWith(`"use strict";`),
              "ondata received decoded data"
            );
            browser.test.sendMessage("onBeforeRequest");
          };

          filter.onerror = () => {
            // onBeforeRequest will always beat the cache race, so we should always
            // get valid data in ondata.
            browser.test.fail("error-received", filter.error);
          };
        },
        {
          urls: ["http://example.com/data/file_script_good.js"],
        },
        ["blocking"]
      );
      browser.webRequest.onHeadersReceived.addListener(
        details => {
          let filter = browser.webRequest.filterResponseData(details.requestId);
          let decoder = new TextDecoder("utf-8");
          let encoder = new TextEncoder();

          // Because cache is always a race, intermittently we will succesfully
          // beat the cache, in which case we pass in ondata.  If cache wins,
          // we pass in onerror.
          // Running the test with --verify hits this cache race issue, as well
          // it seems that the cache primarily looses on linux1804.
          let gotone = false;
          filter.ondata = event => {
            browser.test.assertFalse(gotone, "cache lost the race");
            gotone = true;
            let str = decoder.decode(event.data, { stream: true });
            filter.write(encoder.encode(str));
            filter.disconnect();
            browser.test.assertTrue(
              str.startsWith(`"use strict";`),
              "ondata received decoded data"
            );
            browser.test.sendMessage("onHeadersReceived");
          };

          filter.onerror = () => {
            browser.test.assertFalse(gotone, "cache won the race");
            gotone = true;
            browser.test.assertEq(
              filter.error,
              "Channel is delivering cached alt-data"
            );
            browser.test.sendMessage("onHeadersReceived");
          };
        },
        {
          urls: ["http://example.com/data/file_script_bad.js"],
        },
        ["blocking"]
      );
    },

    manifest: {
      permissions: ["webRequest", "webRequestBlocking", "http://example.com/*"],
    },
  });

  // Prime the cache so we have the script byte-cached.
  let contentPage = await ExtensionTestUtils.loadContentPage(
    "http://example.com/data/file_script.html"
  );
  await contentPage.close();

  await extension.startup();

  let page_cached = await ExtensionTestUtils.loadContentPage(
    "http://example.com/data/file_script.html"
  );
  await Promise.all([
    extension.awaitMessage("onBeforeRequest"),
    extension.awaitMessage("onHeadersReceived"),
  ]);
  await page_cached.close();
  await extension.unload();

  Services.prefs.clearUserPref("dom.script_loader.bytecode_cache.enabled");
  Services.prefs.clearUserPref("dom.script_loader.bytecode_cache.strategy");
});

add_task(async function test_webRequestFilterResponse_permission() {
  function background() {
    browser.test.onMessage.addListener(async (msg, ...args) => {
      if (msg !== "testFilterResponseData") {
        browser.test.fail(`Unexpected test message: ${msg}`);
        return;
      }

      const [{ expectMissingPermissionError }] = args;

      if (expectMissingPermissionError) {
        browser.test.assertThrows(
          () => browser.webRequest.filterResponseData("fake-response-id"),
          /Missing required "webRequestFilterResponse" permission/,
          "Expected missing webRequestFilterResponse permission error"
        );
      } else {
        // Expect the generic error raised on invalid response id
        // if the missing permission error isn't expected.
        browser.test.assertTrue(
          browser.webRequest.filterResponseData("fake-response-id"),
          "Expected no missing webRequestFilterResponse permission error"
        );
      }

      browser.test.notifyPass();
    });
  }

  info(
    "Verify MV2 extension does not require webRequestFilterResponse permission"
  );
  const extMV2 = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      manifest_version: 2,
      permissions: ["webRequest", "webRequestBlocking"],
    },
  });

  await extMV2.startup();
  extMV2.sendMessage("testFilterResponseData", {
    expectMissingPermissionError: false,
  });
  await extMV2.awaitFinish();
  await extMV2.unload();

  info(
    "Verify filterResponseData throws on MV3 extension without webRequestFilterResponse permission"
  );
  const extMV3NoPerm = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      manifest_version: 3,
      permissions: ["webRequest", "webRequestBlocking"],
    },
  });

  await extMV3NoPerm.startup();
  extMV3NoPerm.sendMessage("testFilterResponseData", {
    expectMissingPermissionError: true,
  });
  await extMV3NoPerm.awaitFinish();
  await extMV3NoPerm.unload();

  info(
    "Verify filterResponseData does not throw on MV3 extension without webRequestFilterResponse permission"
  );
  const extMV3WithPerm = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      manifest_version: 3,
      permissions: [
        "webRequest",
        "webRequestBlocking",
        "webRequestFilterResponse",
      ],
    },
  });

  await extMV3WithPerm.startup();
  extMV3WithPerm.sendMessage("testFilterResponseData", {
    expectMissingPermissionError: false,
  });
  await extMV3WithPerm.awaitFinish();
  await extMV3WithPerm.unload();
});
