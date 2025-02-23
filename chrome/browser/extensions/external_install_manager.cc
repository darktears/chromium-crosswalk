// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_install_manager.h"

#include <string>

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/external_install_error.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_source.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_url_handlers.h"

namespace extensions {

namespace {

// Histogram values for logging events related to externally installed
// extensions.
enum ExternalExtensionEvent {
  EXTERNAL_EXTENSION_INSTALLED = 0,
  EXTERNAL_EXTENSION_IGNORED,
  EXTERNAL_EXTENSION_REENABLED,
  EXTERNAL_EXTENSION_UNINSTALLED,
  EXTERNAL_EXTENSION_BUCKET_BOUNDARY,
};

// Prompt the user this many times before considering an extension acknowledged.
const int kMaxExtensionAcknowledgePromptCount = 3;

void LogExternalExtensionEvent(const Extension* extension,
                               ExternalExtensionEvent event) {
  UMA_HISTOGRAM_ENUMERATION("Extensions.ExternalExtensionEvent",
                            event,
                            EXTERNAL_EXTENSION_BUCKET_BOUNDARY);
  if (ManifestURL::UpdatesFromGallery(extension)) {
    UMA_HISTOGRAM_ENUMERATION("Extensions.ExternalExtensionEventWebstore",
                              event,
                              EXTERNAL_EXTENSION_BUCKET_BOUNDARY);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Extensions.ExternalExtensionEventNonWebstore",
                              event,
                              EXTERNAL_EXTENSION_BUCKET_BOUNDARY);
  }
}

}  // namespace

ExternalInstallManager::ExternalInstallManager(
    content::BrowserContext* browser_context,
    bool is_first_run)
    : browser_context_(browser_context),
      is_first_run_(is_first_run),
      extension_prefs_(ExtensionPrefs::Get(browser_context_)),
      extension_registry_observer_(this) {
  DCHECK(browser_context_);
  extension_registry_observer_.Add(ExtensionRegistry::Get(browser_context_));
  registrar_.Add(
      this,
      extensions::NOTIFICATION_EXTENSION_REMOVED,
      content::Source<Profile>(Profile::FromBrowserContext(browser_context_)));
}

ExternalInstallManager::~ExternalInstallManager() {
}

void ExternalInstallManager::AddExternalInstallError(const Extension* extension,
                                                     bool is_new_profile) {
  // Error already exists or has been previously shown.
  if (ContainsKey(errors_, extension->id()) ||
      shown_ids_.count(extension->id()) > 0)
    return;

  ExternalInstallError::AlertType alert_type =
      (ManifestURL::UpdatesFromGallery(extension) && !is_new_profile)
          ? ExternalInstallError::BUBBLE_ALERT
          : ExternalInstallError::MENU_ALERT;

  scoped_ptr<ExternalInstallError> error(new ExternalInstallError(
      browser_context_, extension->id(), alert_type, this));
  shown_ids_.insert(extension->id());
  errors_.insert(std::make_pair(extension->id(), std::move(error)));
}

void ExternalInstallManager::RemoveExternalInstallError(
    const std::string& extension_id) {
  if (errors_.erase(extension_id) > 0)
    UpdateExternalExtensionAlert();
}

void ExternalInstallManager::UpdateExternalExtensionAlert() {
  // If the feature is not enabled do nothing.
  if (!FeatureSwitch::prompt_for_external_extensions()->IsEnabled())
    return;

  // Look for any extensions that were disabled because of being unacknowledged
  // external extensions.
  const ExtensionSet& disabled_extensions =
      ExtensionRegistry::Get(browser_context_)->disabled_extensions();
  for (const scoped_refptr<const Extension>& extension : disabled_extensions) {
    if (ContainsKey(errors_, extension->id()) ||
        shown_ids_.count(extension->id()) > 0)
      continue;

    if (!IsUnacknowledgedExternalExtension(extension.get()))
      continue;

    // Warn the user about the suspicious extension.
    if (extension_prefs_->IncrementAcknowledgePromptCount(extension->id()) >
        kMaxExtensionAcknowledgePromptCount) {
      // Stop prompting for this extension and record metrics.
      extension_prefs_->AcknowledgeExternalExtension(extension->id());
      LogExternalExtensionEvent(extension.get(), EXTERNAL_EXTENSION_IGNORED);
      continue;
    }

    if (is_first_run_)
      extension_prefs_->SetExternalInstallFirstRun(extension->id());

    // |first_run| is true if the extension was installed during a first run
    // (even if it's post-first run now).
    AddExternalInstallError(
        extension.get(),
        extension_prefs_->IsExternalInstallFirstRun(extension->id()));
  }
}

void ExternalInstallManager::AcknowledgeExternalExtension(
    const std::string& id) {
  extension_prefs_->AcknowledgeExternalExtension(id);
  UpdateExternalExtensionAlert();
}

std::vector<ExternalInstallError*>
ExternalInstallManager::GetErrorsForTesting() {
  std::vector<ExternalInstallError*> errors;
  for (auto const& error : errors_)
    errors.push_back(error.second.get());
  return errors;
}

void ExternalInstallManager::OnExtensionLoaded(
    content::BrowserContext* browser_context,
    const Extension* extension) {
  if (!IsUnacknowledgedExternalExtension(extension))
    return;

  // We treat loading as acknowledgement (since the user consciously chose to
  // re-enable the extension).
  AcknowledgeExternalExtension(extension->id());
  LogExternalExtensionEvent(extension, EXTERNAL_EXTENSION_REENABLED);

  // If we had an error for this extension, remove it.
  RemoveExternalInstallError(extension->id());
}

void ExternalInstallManager::OnExtensionInstalled(
    content::BrowserContext* browser_context,
    const Extension* extension,
    bool is_update) {
  // Certain extension locations are specific enough that we can
  // auto-acknowledge any extension that came from one of them.
  if (Manifest::IsPolicyLocation(extension->location()) ||
      extension->location() == Manifest::EXTERNAL_COMPONENT) {
    AcknowledgeExternalExtension(extension->id());
    return;
  }

  if (!IsUnacknowledgedExternalExtension(extension))
    return;

  LogExternalExtensionEvent(extension, EXTERNAL_EXTENSION_INSTALLED);

  UpdateExternalExtensionAlert();
}

void ExternalInstallManager::OnExtensionUninstalled(
    content::BrowserContext* browser_context,
    const Extension* extension,
    extensions::UninstallReason reason) {
  if (IsUnacknowledgedExternalExtension(extension))
    LogExternalExtensionEvent(extension, EXTERNAL_EXTENSION_UNINSTALLED);
}

bool ExternalInstallManager::IsUnacknowledgedExternalExtension(
    const Extension* extension) const {
  if (!FeatureSwitch::prompt_for_external_extensions()->IsEnabled())
    return false;

  return (Manifest::IsExternalLocation(extension->location()) &&
          !extension_prefs_->IsExternalExtensionAcknowledged(extension->id()) &&
          !(extension_prefs_->GetDisableReasons(extension->id()) &
            Extension::DISABLE_SIDELOAD_WIPEOUT));
}

void ExternalInstallManager::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(extensions::NOTIFICATION_EXTENSION_REMOVED, type);
  // The error is invalidated if the extension has been loaded or removed.
  // It's a shame we have to use the notification system (instead of the
  // registry observer) for this, but the ExtensionUnloaded notification is
  // not sent out if the extension is disabled (which it is here).
  const std::string& extension_id =
      content::Details<const Extension>(details).ptr()->id();
  if (ContainsKey(errors_, extension_id))
    RemoveExternalInstallError(extension_id);
}

}  // namespace extensions
