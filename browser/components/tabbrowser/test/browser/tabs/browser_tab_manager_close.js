/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const URL1 = "data:text/plain,tab1";
const URL2 = "data:text/plain,tab2";
const URL3 = "data:text/plain,tab3";
const URL4 = "data:text/plain,tab4";
const URL5 = "data:text/plain,tab5";

/**
 * Tests that middle-clicking on a tab in the Tab Manager will close it.
 */
add_task(async function test_tab_manager_close_middle_click() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  win.gTabsPanel.init();
  await addTabTo(win.gBrowser, URL1);
  await addTabTo(win.gBrowser, URL2);
  await addTabTo(win.gBrowser, URL3);
  await addTabTo(win.gBrowser, URL4);
  await addTabTo(win.gBrowser, URL5);

  let button = win.document.getElementById("alltabs-button");
  let allTabsView = win.document.getElementById("allTabsMenu-allTabsView");
  let allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
    allTabsView,
    "ViewShown"
  );
  button.click();
  await allTabsPopupShownPromise;

  let list = win.document.getElementById("allTabsMenu-allTabsView-tabs");
  while (win.gBrowser.tabs.length > 1) {
    let row = list.lastElementChild;
    let tabClosing = BrowserTestUtils.waitForTabClosing(row.tab);
    EventUtils.synthesizeMouseAtCenter(row, { button: 1 }, win);
    await tabClosing;
    Assert.ok(true, "Closed a tab with middle-click.");
  }
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Tests that clicking the close button next to a tab manager item
 * will close it.
 */
add_task(async function test_tab_manager_close_button() {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  win.gTabsPanel.init();
  let pinnedTab = await addTabTo(win.gBrowser, URL1);
  win.gBrowser.pinTab(pinnedTab);
  await addTabTo(win.gBrowser, URL2);
  await addTabTo(win.gBrowser, URL3);
  await addTabTo(win.gBrowser, URL4);
  await addTabTo(win.gBrowser, URL5);

  let button = win.document.getElementById("alltabs-button");
  let allTabsView = win.document.getElementById("allTabsMenu-allTabsView");
  let allTabsPopupShownPromise = BrowserTestUtils.waitForEvent(
    allTabsView,
    "ViewShown"
  );
  button.click();
  await allTabsPopupShownPromise;

  let list = win.document.getElementById("allTabsMenu-allTabsView-tabs");

  let pinnedTabRow = list.firstElementChild;
  Assert.ok(pinnedTabRow.tab.pinned, "first item is for the pinned tab");
  Assert.ok(
    !pinnedTabRow.querySelector(".all-tabs-close-button"),
    "row for pinned tab doesn't have a close button"
  );

  // Disable the tab closing animation so tabs are removed immediately. This simplifies the test.
  win.gReduceMotionOverride = true;
  while (win.gBrowser.tabs.length > 1) {
    let row = list.lastElementChild;
    Assert.ok(!row.tab.pinned, "Tab for last row is not pinned");
    let tabClosing = BrowserTestUtils.waitForTabClosing(row.tab);
    let closeButton = row.querySelector(".all-tabs-close-button");
    Assert.ok(closeButton, "row for last tab has a close button");
    EventUtils.synthesizeMouseAtCenter(closeButton, { button: 1 }, win);
    await tabClosing;
    Assert.ok(true, "Closed a tab with the close button.");
  }
  await BrowserTestUtils.closeWindow(win);
});
