// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/first_run/first_run_internal.h"

#include <windows.h>
#include <shellapi.h>
#include <stdint.h>

#include "base/base_paths.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/time/time.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/locale_settings.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/master_preferences_constants.h"
#include "chrome/installer/util/util_constants.h"
#include "content/public/browser/browser_thread.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/win/shell.h"

namespace {

// Launches the setup exe with the given parameter/value on the command-line.
// For non-metro Windows, it waits for its termination, returns its exit code
// in |*ret_code|, and returns true if the exit code is valid.
// For metro Windows, it launches setup via ShellExecuteEx and returns in order
// to bounce the user back to the desktop, then returns immediately.
bool LaunchSetupForEula(const base::FilePath::StringType& value,
                        int* ret_code) {
  base::FilePath exe_dir;
  if (!PathService::Get(base::DIR_MODULE, &exe_dir))
    return false;
  exe_dir = exe_dir.Append(installer::kInstallerDir);
  base::FilePath exe_path = exe_dir.Append(installer::kSetupExe);

  base::CommandLine cl(base::CommandLine::NO_PROGRAM);
  cl.AppendSwitchNative(installer::switches::kShowEula, value);

  base::CommandLine setup_path(exe_path);
  setup_path.AppendArguments(cl, false);

  base::Process process =
      base::LaunchProcess(setup_path, base::LaunchOptions());
  int exit_code = 0;
  if (!process.IsValid() || !process.WaitForExit(&exit_code))
    return false;

  *ret_code = exit_code;
  return true;
}

// Returns true if the EULA is required but has not been accepted by this user.
// The EULA is considered having been accepted if the user has gotten past
// first run in the "other" environment (desktop or metro).
bool IsEULANotAccepted(installer::MasterPreferences* install_prefs) {
  bool value = false;
  if (install_prefs->GetBool(installer::master_preferences::kRequireEula,
          &value) && value) {
    base::FilePath eula_sentinel;
    // Be conservative and show the EULA if the path to the sentinel can't be
    // determined.
    if (!InstallUtil::GetEULASentinelFilePath(&eula_sentinel) ||
        !base::PathExists(eula_sentinel)) {
      return true;
    }
  }
  return false;
}

// Writes the EULA to a temporary file, returned in |*eula_path|, and returns
// true if successful.
bool WriteEULAtoTempFile(base::FilePath* eula_path) {
  std::string terms = l10n_util::GetStringUTF8(IDS_TERMS_HTML);
  return (!terms.empty() &&
          base::CreateTemporaryFile(eula_path) &&
          base::WriteFile(*eula_path, terms.data(), terms.size()) != -1);
}

// Creates the sentinel indicating that the EULA was required and has been
// accepted.
bool CreateEULASentinel() {
  base::FilePath eula_sentinel;
  return InstallUtil::GetEULASentinelFilePath(&eula_sentinel) &&
      base::CreateDirectory(eula_sentinel.DirName()) &&
      base::WriteFile(eula_sentinel, "", 0) != -1;
}

}  // namespace

namespace first_run {
namespace internal {

void DoPostImportPlatformSpecificTasks(Profile* /* profile */) {
  // Trigger the Active Setup command for system-level Chromes to finish
  // configuring this user's install (e.g. per-user shortcuts).
  // Delay the task slightly to give Chrome launch I/O priority while also
  // making sure shortcuts are created promptly to avoid annoying the user by
  // re-creating shortcuts he previously deleted.
  static const int64_t kTiggerActiveSetupDelaySeconds = 5;
  base::FilePath chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe)) {
    NOTREACHED();
  } else if (!InstallUtil::IsPerUserInstall(chrome_exe)) {
    content::BrowserThread::GetBlockingPool()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&InstallUtil::TriggerActiveSetupCommand),
        base::TimeDelta::FromSeconds(kTiggerActiveSetupDelaySeconds));
  }
}

bool IsFirstRunSentinelPresent() {
  base::FilePath sentinel;
  if (!GetFirstRunSentinelFilePath(&sentinel) || base::PathExists(sentinel))
    return true;

  // Copy any legacy first run sentinel file for Windows user-level installs
  // from the application directory to the user data directory.
  base::FilePath exe_path;
  if (PathService::Get(base::DIR_EXE, &exe_path) &&
      InstallUtil::IsPerUserInstall(exe_path)) {
    base::FilePath legacy_sentinel = exe_path.Append(chrome::kFirstRunSentinel);
    if (base::PathExists(legacy_sentinel)) {
      // Copy the file instead of moving it to avoid breaking developer builds
      // where the sentinel is dropped beside chrome.exe by a build action.
      bool migrated = base::CopyFile(legacy_sentinel, sentinel);
      DPCHECK(migrated);
      // The sentinel is present regardless of whether or not it was migrated.
      return true;
    }
  }

  return false;
}

bool ShowPostInstallEULAIfNeeded(installer::MasterPreferences* install_prefs) {
  if (IsEULANotAccepted(install_prefs)) {
    // Show the post-installation EULA. This is done by setup.exe and the
    // result determines if we continue or not. We wait here until the user
    // dismisses the dialog.

    // The actual eula text is in a resource in chrome. We extract it to
    // a text file so setup.exe can use it as an inner frame.
    base::FilePath inner_html;
    if (WriteEULAtoTempFile(&inner_html)) {
      int retcode = 0;
      if (!LaunchSetupForEula(inner_html.value(), &retcode) ||
          (retcode != installer::EULA_ACCEPTED &&
           retcode != installer::EULA_ACCEPTED_OPT_IN)) {
        LOG(WARNING) << "EULA flow requires fast exit.";
        return false;
      }
      CreateEULASentinel();

      if (retcode == installer::EULA_ACCEPTED) {
        DVLOG(1) << "EULA : no collection";
        GoogleUpdateSettings::SetCollectStatsConsent(false);
      } else if (retcode == installer::EULA_ACCEPTED_OPT_IN) {
        DVLOG(1) << "EULA : collection consent";
        GoogleUpdateSettings::SetCollectStatsConsent(true);
      }
    }
  }
  return true;
}

base::FilePath MasterPrefsPath() {
  // The standard location of the master prefs is next to the chrome binary.
  base::FilePath master_prefs;
  if (!PathService::Get(base::DIR_EXE, &master_prefs))
    return base::FilePath();
  return master_prefs.AppendASCII(installer::kDefaultMasterPrefs);
}

}  // namespace internal
}  // namespace first_run
