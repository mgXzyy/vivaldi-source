// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {SourcesTestRunner} from 'sources_test_runner';

(async function() {
  TestRunner.addResult(`Tests that debugger will copy valid stack trace upon context menu action.\n`);
  await TestRunner.loadLegacyModule('sources');
  await TestRunner.showPanel('sources');
  await TestRunner.evaluateInPagePromise(`
      function testFunction()
      {
          functionBar();
      }

      function functionBar()
      {
          functionBaz();
      }

      function functionBaz()
      {
          debugger;
      }
  `);

  SourcesTestRunner.startDebuggerTest(step1);

  function step1() {
    SourcesTestRunner.runTestFunctionAndWaitUntilPaused();
    TestRunner.addSniffer(Sources.CallStackSidebarPane.prototype, 'updatedForTest', step2);
  }

  function step2() {
    InspectorFrontendHost.copyText = text => TestRunner.addResult(TestRunner.clearSpecificInfoFromStackFrames(text));
    Sources.CallStackSidebarPane.instance().copyStackTrace();
    SourcesTestRunner.completeDebuggerTest();
  }
})();
