// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTERNAL_PROTOCOL_EXTERNAL_PROTOCOL_OBSERVER_H_
#define CHROME_BROWSER_EXTERNAL_PROTOCOL_EXTERNAL_PROTOCOL_OBSERVER_H_

#include "base/macros.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

// ExternalProtocolObserver is responsible for handling messages from
// WebContents relating to external protocols.
class ExternalProtocolObserver
    : public content::WebContentsObserver,
      public content::WebContentsUserData<ExternalProtocolObserver> {
 public:
  ~ExternalProtocolObserver() override;

  // content::WebContentsObserver overrides.
  void DidGetUserGesture() override;

 private:
  explicit ExternalProtocolObserver(content::WebContents* web_contents);
  friend class content::WebContentsUserData<ExternalProtocolObserver>;

  DISALLOW_COPY_AND_ASSIGN(ExternalProtocolObserver);
};

#endif  // CHROME_BROWSER_EXTERNAL_PROTOCOL_EXTERNAL_PROTOCOL_OBSERVER_H_
