/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* import-globals-from head.js */

// Test that Console.jsm outputs messages to the Browser Console.

"use strict";

add_task(async function testCategoryLogs() {
  let consoleStorage = Cc["@mozilla.org/consoleAPI-storage;1"];
  let storage = consoleStorage.getService(Ci.nsIConsoleAPIStorage);
  storage.clearEvents();

  let {console} = ChromeUtils.import("resource://gre/modules/Console.jsm", {});
  console.log("bug861338-log-cached");

  let hud = await HUDService.toggleBrowserConsole();

  await checkMessageExists(hud, "bug861338-log-cached");

  hud.jsterm.clearOutput(true);

  function testTrace() {
    console.trace();
  }

  console.time("foobarTimer");
  let foobar = { bug851231prop: "bug851231value" };

  console.log("bug851231-log");
  console.info("bug851231-info");
  console.warn("bug851231-warn");
  console.error("bug851231-error", foobar);
  console.debug("bug851231-debug");
  console.dir(document);
  testTrace();
  console.timeEnd("foobarTimer");

  info("wait for the Console.jsm messages");

  await checkMessageExists(hud, "bug851231-log");
  await checkMessageExists(hud, "bug851231-info");
  await checkMessageExists(hud, "bug851231-warn");
  await checkMessageExists(hud, "bug851231-error");
  await checkMessageExists(hud, "bug851231-debug");
  await checkMessageExists(hud, "XULDocument");
  await checkMessageExists(hud, "console.trace()");
  await checkMessageExists(hud, "foobarTimer");

  hud.jsterm.clearOutput(true);
  await HUDService.toggleBrowserConsole();
});

add_task(async function testFilter() {
  let consoleStorage = Cc["@mozilla.org/consoleAPI-storage;1"];
  let storage = consoleStorage.getService(Ci.nsIConsoleAPIStorage);
  storage.clearEvents();

  let {ConsoleAPI} = ChromeUtils.import("resource://gre/modules/Console.jsm", {});
  let console2 = new ConsoleAPI();
  let hud = await HUDService.toggleBrowserConsole();

  // Enable the error category and disable the log category.
  await setFilterState(hud, {
    error: true,
    log: false
  });

  let shouldBeVisible = "Should be visible";
  let shouldBeHidden = "Should be hidden";

  console2.log(shouldBeHidden);
  console2.error(shouldBeVisible);

  await checkMessageExists(hud, shouldBeVisible);
  // Here we can safely assert that the log message is not visible, since the
  // error message was logged after and is visible.
  await checkMessageHidden(hud, shouldBeHidden);

  await resetFilters(hud);
  hud.jsterm.clearOutput(true);
  await HUDService.toggleBrowserConsole();
});

// Test that console.profile / profileEnd trigger the right events
add_task(async function testProfile() {
  let consoleStorage = Cc["@mozilla.org/consoleAPI-storage;1"];
  let storage = consoleStorage.getService(Ci.nsIConsoleAPIStorage);
  let { console } = ChromeUtils.import("resource://gre/modules/Console.jsm", {});

  storage.clearEvents();

  let profilerEvents = [];

  function observer(subject, topic) {
    is(topic, "console-api-profiler", "The topic is 'console-api-profiler'");
    const subjectObj = subject.wrappedJSObject;
    const event = { action: subjectObj.action, name: subjectObj.arguments[0] };
    info(`Profiler event: action=${event.action}, name=${event.name}`);
    profilerEvents.push(event);
  }

  Services.obs.addObserver(observer, "console-api-profiler");

  console.profile("test");
  console.profileEnd("test");

  Services.obs.removeObserver(observer, "console-api-profiler");

  // Test that no messages were logged to the storage
  let consoleEvents = storage.getEvents();
  is(consoleEvents.length, 0, "There are zero logged messages");

  // Test that two profiler events were fired
  is(profilerEvents.length, 2, "Got two profiler events");
  is(profilerEvents[0].action, "profile", "First event has the right action");
  is(profilerEvents[0].name, "test", "First event has the right name");
  is(profilerEvents[1].action, "profileEnd", "Second event has the right action");
  is(profilerEvents[1].name, "test", "Second event has the right name");
});

async function checkMessageExists(hud, msg) {
  info(`Checking "${msg}" was logged`);
  let message = await waitFor(() => findMessage(hud, msg));
  ok(message, `"${msg}" was logged`);
}

async function checkMessageHidden(hud, msg) {
  info(`Checking "${msg}" was not logged`);
  await waitFor(() => findMessage(hud, msg) == null);
  ok(true, `"${msg}" was not logged`);
}