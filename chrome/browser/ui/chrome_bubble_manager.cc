// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/chrome_bubble_manager.h"

#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/bubble/bubble_controller.h"
#include "components/bubble/bubble_delegate.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"

namespace {

// Add any new enum before BUBBLE_TYPE_MAX and update BubbleType in
// tools/metrics/histograms/histograms.xml.
// Do not re-number these because they are used for collecting metrics.
// Related bubbles can be grouped together every 10 values.
enum BubbleType {
  // Special bubble values:
  BUBBLE_TYPE_UNKNOWN = 0,  // Used for unrecognized bubble names.
  BUBBLE_TYPE_MOCK = 1,     // Used for testing.

  // Extension-related bubbles:
  BUBBLE_TYPE_EXTENSION_INSTALLED = 10,  // Displayed after installing.

  // Translation-related bubbles:
  BUBBLE_TYPE_TRANSLATE = 20,  // Displays a request to translate a page.

  // Permissions-related bubbles:
  BUBBLE_TYPE_PERMISSION = 30,  // Displays a permission request to the user.
  BUBBLE_TYPE_CHOOSER_PERMISSION = 31,  // For chooser permissions.

  // Upper boundary for metrics.
  BUBBLE_TYPE_MAX,
};

// TODO(juncai): Since LogBubbleCloseReason function adds metrics for each
// close type, we can use only enum, and it may not be necessary to keep the
// bubble name.
// Convert from bubble name to ID. The bubble ID will allow collecting the
// close reason for each bubble type.
static int GetBubbleId(BubbleReference bubble) {
  BubbleType bubble_type = BUBBLE_TYPE_UNKNOWN;

  // Translate from bubble name to enum.
  if (bubble->GetName().compare("MockBubble") == 0)
    bubble_type = BUBBLE_TYPE_MOCK;
  else if (bubble->GetName().compare("ExtensionInstalled") == 0)
    bubble_type = BUBBLE_TYPE_EXTENSION_INSTALLED;
  else if (bubble->GetName().compare("TranslateBubble") == 0)
    bubble_type = BUBBLE_TYPE_TRANSLATE;
  else if (bubble->GetName().compare("PermissionBubble") == 0)
    bubble_type = BUBBLE_TYPE_PERMISSION;
  else if (bubble->GetName().compare("ChooserBubble") == 0)
    bubble_type = BUBBLE_TYPE_CHOOSER_PERMISSION;

  DCHECK_NE(bubble_type, BUBBLE_TYPE_UNKNOWN);
  DCHECK_NE(bubble_type, BUBBLE_TYPE_MAX);

  return bubble_type;
}

// Log the reason for closing this bubble.
// Each reason is its own metric. Each histogram call MUST have a runtime
// constant value passed in for the title.
static void LogBubbleCloseReason(BubbleReference bubble,
                                 BubbleCloseReason reason) {
  int bubble_id = GetBubbleId(bubble);
  switch (reason) {
    case BUBBLE_CLOSE_FORCED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.Forced", bubble_id);
      return;
    case BUBBLE_CLOSE_FOCUS_LOST:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.FocusLost", bubble_id);
      return;
    case BUBBLE_CLOSE_TABSWITCHED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.TabSwitched", bubble_id);
      return;
    case BUBBLE_CLOSE_TABDETACHED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.TabDetached", bubble_id);
      return;
    case BUBBLE_CLOSE_USER_DISMISSED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.UserDismissed", bubble_id);
      return;
    case BUBBLE_CLOSE_NAVIGATED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.Navigated", bubble_id);
      return;
    case BUBBLE_CLOSE_FULLSCREEN_TOGGLED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.FullscreenToggled", bubble_id);
      return;
    case BUBBLE_CLOSE_ACCEPTED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.Accepted", bubble_id);
      return;
    case BUBBLE_CLOSE_CANCELED:
      UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.Close.Canceled", bubble_id);
      return;
  }

  NOTREACHED();
}

}  // namespace

ChromeBubbleManager::ChromeBubbleManager(TabStripModel* tab_strip_model)
    : tab_strip_model_(tab_strip_model) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(tab_strip_model_);
  tab_strip_model_->AddObserver(this);
  AddBubbleManagerObserver(&chrome_bubble_metrics_);
}

ChromeBubbleManager::~ChromeBubbleManager() {
  tab_strip_model_->RemoveObserver(this);

  // Finalize requests before removing the BubbleManagerObserver so it can
  // collect metrics when closing any open bubbles.
  FinalizePendingRequests();
  RemoveBubbleManagerObserver(&chrome_bubble_metrics_);
}

void ChromeBubbleManager::TabDetachedAt(content::WebContents* contents,
                                        int index) {
  CloseAllBubbles(BUBBLE_CLOSE_TABDETACHED);
  // Any bubble that didn't close should update its anchor position.
  UpdateAllBubbleAnchors();
}

void ChromeBubbleManager::TabDeactivated(content::WebContents* contents) {
  CloseAllBubbles(BUBBLE_CLOSE_TABSWITCHED);
}

void ChromeBubbleManager::ActiveTabChanged(content::WebContents* old_contents,
                                           content::WebContents* new_contents,
                                           int index,
                                           int reason) {
  Observe(new_contents);
}

void ChromeBubbleManager::DidToggleFullscreenModeForTab(
    bool entered_fullscreen) {
  CloseAllBubbles(BUBBLE_CLOSE_FULLSCREEN_TOGGLED);
  // Any bubble that didn't close should update its anchor position.
  UpdateAllBubbleAnchors();
}

void ChromeBubbleManager::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  CloseAllBubbles(BUBBLE_CLOSE_NAVIGATED);
}

void ChromeBubbleManager::ChromeBubbleMetrics::OnBubbleNeverShown(
    BubbleReference bubble) {
  UMA_HISTOGRAM_SPARSE_SLOWLY("Bubbles.NeverShown", GetBubbleId(bubble));
}

void ChromeBubbleManager::ChromeBubbleMetrics::OnBubbleClosed(
    BubbleReference bubble, BubbleCloseReason reason) {
  // Log the amount of time the bubble was visible.
  base::TimeDelta visible_time = bubble->GetVisibleTime();
  UMA_HISTOGRAM_LONG_TIMES("Bubbles.DisplayTime.All", visible_time);

  LogBubbleCloseReason(bubble, reason);
}
