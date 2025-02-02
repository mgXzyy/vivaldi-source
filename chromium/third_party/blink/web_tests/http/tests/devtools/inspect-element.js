// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {ConsoleTestRunner} from 'console_test_runner';
import {ElementsTestRunner} from 'elements_test_runner';

(async function() {
  TestRunner.addResult(
      `Tests that inspect element action works for iframe children (https://bugs.webkit.org/show_bug.cgi?id=76808).\n`);
  await TestRunner.loadLegacyModule('console');
  await TestRunner.loadLegacyModule('elements');
  await TestRunner.showPanel('elements');

  await TestRunner.addIframe('resources/inspect-element-iframe.html');

  ElementsTestRunner.firstElementsTreeOutline().addEventListener(
      Elements.ElementsTreeOutline.Events.SelectedNodeChanged, selectedNodeChanged, this);
  function selectedNodeChanged(event) {
    var node = event.data.node;
    if (!node)
      return;
    if (node.getAttribute('id') == 'div') {
      TestRunner.addResult(Elements.DOMPath.fullQualifiedSelector(node));
      TestRunner.completeTest();
    }
  }
  ConsoleTestRunner.evaluateInConsole('inspect(iframeDivElement)');
})();
