// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UTILITY_CHROME_CONTENT_UTILITY_CLIENT_H_
#define CHROME_UTILITY_CHROME_CONTENT_UTILITY_CLIENT_H_

#include <stdint.h>

#include <set>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/scoped_vector.h"
#include "build/build_config.h"
#include "content/public/utility/content_utility_client.h"
#include "ipc/ipc_platform_file.h"

namespace base {
class FilePath;
struct FileDescriptor;
}

class UtilityMessageHandler;

class ChromeContentUtilityClient : public content::ContentUtilityClient {
 public:
  ChromeContentUtilityClient();
  ~ChromeContentUtilityClient() override;

  void UtilityThreadStarted() override;
  bool OnMessageReceived(const IPC::Message& message) override;
  void RegisterMojoServices(content::ServiceRegistry* registry) override;

  void AddHandler(scoped_ptr<UtilityMessageHandler> handler);

  static void PreSandboxStartup();

  // Shared with extensions::ExtensionsHandler.
  static SkBitmap DecodeImage(const std::vector<unsigned char>& encoded_data,
                              bool shrink_to_fit);
  static void DecodeImageAndSend(const std::vector<unsigned char>& encoded_data,
                                 bool shrink_to_fit,
                                 int request_id);

  static void set_max_ipc_message_size_for_test(int64_t max_message_size) {
    max_ipc_message_size_ = max_message_size;
  }

 private:
  // IPC message handlers.
  void OnUnpackWebResource(const std::string& resource_data);
  void OnDecodeImage(const std::vector<unsigned char>& encoded_data,
                     bool shrink_to_fit,
                     int request_id);
#if defined(OS_CHROMEOS)
  void OnRobustJPEGDecodeImage(const std::vector<unsigned char>& encoded_data,
                               int request_id);

  void OnCreateZipFile(const base::FilePath& src_dir,
                       const std::vector<base::FilePath>& src_relative_paths,
                       const base::FileDescriptor& dest_fd);
#endif  // defined(OS_CHROMEOS)

  void OnPatchFileBsdiff(const base::FilePath& input_file,
                         const base::FilePath& patch_file,
                         const base::FilePath& output_file);
  void OnPatchFileCourgette(const base::FilePath& input_file,
                            const base::FilePath& patch_file,
                            const base::FilePath& output_file);
  void OnStartupPing();
#if defined(FULL_SAFE_BROWSING)
  void OnAnalyzeZipFileForDownloadProtection(
      const IPC::PlatformFileForTransit& zip_file,
      const IPC::PlatformFileForTransit& temp_file);
#if defined(OS_MACOSX)
  void OnAnalyzeDmgFileForDownloadProtection(
      const IPC::PlatformFileForTransit& dmg_file);
#endif  // defined(OS_MACOSX)
#endif  // defined(FULL_SAFE_BROWSING)

  typedef ScopedVector<UtilityMessageHandler> Handlers;
  Handlers handlers_;

  // Flag to enable whitelisting.
  bool filter_messages_;
  // A list of message_ids to filter.
  std::set<int> message_id_whitelist_;
  // Maximum IPC msg size (default to kMaximumMessageSize; override for testing)
  static int64_t max_ipc_message_size_;

  DISALLOW_COPY_AND_ASSIGN(ChromeContentUtilityClient);
};

#endif  // CHROME_UTILITY_CHROME_CONTENT_UTILITY_CLIENT_H_
