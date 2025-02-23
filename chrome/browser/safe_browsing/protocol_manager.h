// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_PROTOCOL_MANAGER_H_
#define CHROME_BROWSER_SAFE_BROWSING_PROTOCOL_MANAGER_H_

// A class that implements Chrome's interface with the SafeBrowsing protocol.
// See https://developers.google.com/safe-browsing/developers_guide_v2 for
// protocol details.
//
// The SafeBrowsingProtocolManager handles formatting and making requests of,
// and handling responses from, Google's SafeBrowsing servers. This class uses
// The SafeBrowsingProtocolParser class to do the actual parsing.

#include <deque>
#include <set>
#include <string>
#include <vector>

#include <stddef.h>

#include "base/containers/hash_tables.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/non_thread_safe.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/safe_browsing/chunk_range.h"
#include "chrome/browser/safe_browsing/protocol_manager_helper.h"
#include "chrome/browser/safe_browsing/protocol_parser.h"
#include "chrome/browser/safe_browsing/safe_browsing_util.h"
#include "components/safe_browsing_db/safebrowsing.pb.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_status.h"
#include "url/gurl.h"

namespace net {
class URLFetcher;
class URLRequestContextGetter;
}  // namespace net

namespace safe_browsing {

class SBProtocolManagerFactory;
class SafeBrowsingProtocolManagerDelegate;

class SafeBrowsingProtocolManager : public net::URLFetcherDelegate,
                                    public base::NonThreadSafe {
 public:
  // FullHashCallback is invoked when GetFullHash completes.
  // Parameters:
  //   - The vector of full hash results. If empty, indicates that there
  //     were no matches, and that the resource is safe.
  //   - The cache lifetime of the result. A lifetime of 0 indicates the results
  //     should not be cached.
  typedef base::Callback<void(const std::vector<SBFullHashResult>&,
                              const base::TimeDelta&)> FullHashCallback;

  ~SafeBrowsingProtocolManager() override;

  // Makes the passed |factory| the factory used to instantiate
  // a SafeBrowsingService. Useful for tests.
  static void RegisterFactory(SBProtocolManagerFactory* factory) {
    factory_ = factory;
  }

  // Create an instance of the safe browsing protocol manager.
  static SafeBrowsingProtocolManager* Create(
      SafeBrowsingProtocolManagerDelegate* delegate,
      net::URLRequestContextGetter* request_context_getter,
      const SafeBrowsingProtocolConfig& config);

  // Sets up the update schedule and internal state for making periodic requests
  // of the Safebrowsing servers.
  virtual void Initialize();

  // net::URLFetcherDelegate interface.
  void OnURLFetchComplete(const net::URLFetcher* source) override;

  // Retrieve the full hash for a set of prefixes, and invoke the callback
  // argument when the results are retrieved. The callback may be invoked
  // synchronously.
  virtual void GetFullHash(const std::vector<SBPrefix>& prefixes,
                           FullHashCallback callback,
                           bool is_download,
                           bool is_extended_reporting);

  // Retrieve the full hash for a set of prefixes, and invoke the callback
  // argument when the results are retrieved. The callback may be invoked
  // synchronously. Uses the V4 Safe Browsing protocol.
  virtual void GetV4FullHashes(const std::vector<SBPrefix>& prefixes,
                               const std::vector<PlatformType>& platforms,
                               ThreatType threat_type,
                               FullHashCallback callback);

  // Retrieve the full hash and API metadata for a set of prefixes, and invoke
  // the callback argument when the results are retrieved. The callback may be
  // invoked synchronously. Uses the V4 Safe Browsing protocol.
  virtual void GetFullHashesWithApis(const std::vector<SBPrefix>& prefixes,
                                     FullHashCallback callback);

  // Forces the start of next update after |interval| time.
  void ForceScheduleNextUpdate(base::TimeDelta interval);

  // Scheduled update callback.
  void GetNextUpdate();

  // Called by the SafeBrowsingService when our request for a list of all chunks
  // for each list is done.  If database_error is true, that means the protocol
  // manager shouldn't fetch updates since they can't be written to disk.  It
  // should try again later to open the database.
  void OnGetChunksComplete(const std::vector<SBListChunkRanges>& list,
                           bool database_error,
                           bool is_extended_reporting);

  // The last time we received an update.
  base::Time last_update() const { return last_update_; }

  // Setter for additional_query_. To make sure the additional_query_ won't
  // be changed in the middle of an update, caller (e.g.: SafeBrowsingService)
  // should call this after callbacks triggered in UpdateFinished() or before
  // IssueUpdateRequest().
  void set_additional_query(const std::string& query) {
    additional_query_ = query;
  }
  const std::string& additional_query() const {
    return additional_query_;
  }

  // Enumerate failures for histogramming purposes.  DO NOT CHANGE THE
  // ORDERING OF THESE VALUES.
  enum ResultType {
    // 200 response code means that the server recognized the hash
    // prefix, while 204 is an empty response indicating that the
    // server did not recognize it.
    GET_HASH_STATUS_200,
    GET_HASH_STATUS_204,

    // Subset of successful responses which returned no full hashes.
    // This includes the STATUS_204 case, and the *_ERROR cases.
    GET_HASH_FULL_HASH_EMPTY,

    // Subset of successful responses for which one or more of the
    // full hashes matched (should lead to an interstitial).
    GET_HASH_FULL_HASH_HIT,

    // Subset of successful responses which weren't empty and have no
    // matches.  It means that there was a prefix collision which was
    // cleared up by the full hashes.
    GET_HASH_FULL_HASH_MISS,

    // Subset of successful responses where the response body wasn't parsable.
    GET_HASH_PARSE_ERROR,

    // Gethash request failed (network error).
    GET_HASH_NETWORK_ERROR,

    // Gethash request returned HTTP result code other than 200 or 204.
    GET_HASH_HTTP_ERROR,

    // Gethash attempted during error backoff, no request sent.
    GET_HASH_BACKOFF_ERROR,

    // Gethash attempted before min wait duration elapsed, no request sent.
    GET_HASH_MIN_WAIT_DURATION_ERROR,

    // Memory space for histograms is determined by the max.  ALWAYS
    // ADD NEW VALUES BEFORE THIS ONE.
    GET_HASH_RESULT_MAX
  };

  // Record a GetHash result. |is_download| indicates if the get
  // hash is triggered by download related lookup.
  static void RecordGetHashResult(bool is_download,
                                  ResultType result_type);

  // Record a V4 GetHash result.
  static void RecordGetV4HashResult(ResultType result_type);

  // Record HTTP response code when there's no error in fetching an HTTP
  // request, and the error code, when there is.
  // |metric_name| is the name of the UMA metric to record the response code or
  // error code against, |status| represents the status of the HTTP request, and
  // |response code| represents the HTTP response code received from the server.
  static void RecordHttpResponseOrErrorCode(
      const char* metric_name, const net::URLRequestStatus& status,
      int response_code);

  // Returns whether another update is currently scheduled.
  bool IsUpdateScheduled() const;

  // Called when app changes status of foreground or background.
  void SetAppInForeground(bool foreground) {
    app_in_foreground_ = foreground;
  }

 protected:
  // Constructs a SafeBrowsingProtocolManager for |delegate| that issues
  // network requests using |request_context_getter|.
  SafeBrowsingProtocolManager(
      SafeBrowsingProtocolManagerDelegate* delegate,
      net::URLRequestContextGetter* request_context_getter,
      const SafeBrowsingProtocolConfig& config);

 private:
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest, TestBackOffTimes);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest, TestChunkStrings);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest, TestGetHashUrl);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestGetV4HashUrl);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestGetV4HashRequest);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestParseV4HashResponse);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestParseV4HashResponseWrongThreatEntryType);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestParseV4HashResponseSocialEngineeringThreatType);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestParseV4HashResponseNonPermissionMetadata);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestGetHashBackOffTimes);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest,
                           TestGetV4HashBackOffTimes);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest, TestNextChunkUrl);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingProtocolManagerTest, TestUpdateUrl);
  friend class SafeBrowsingServerTest;
  friend class SBProtocolManagerFactoryImpl;

  // Internal API for fetching information from the SafeBrowsing servers. The
  // GetHash requests are higher priority since they can block user requests
  // so are handled separately.
  enum SafeBrowsingRequestType {
    NO_REQUEST = 0,     // No requests in progress
    UPDATE_REQUEST,     // Request for redirect URLs
    BACKUP_UPDATE_REQUEST, // Request for redirect URLs to a backup URL.
    CHUNK_REQUEST,      // Request for a specific chunk
  };

  // Which type of backup update request is being used.
  enum BackupUpdateReason {
    BACKUP_UPDATE_REASON_CONNECT,
    BACKUP_UPDATE_REASON_HTTP,
    BACKUP_UPDATE_REASON_NETWORK,
    BACKUP_UPDATE_REASON_MAX,
  };

  // Generates Update URL for querying about the latest set of chunk updates.
  GURL UpdateUrl(bool is_extended_reporting) const;

  // Generates backup Update URL for querying about the latest set of chunk
  // updates. |url_prefix| is the base prefix to use.
  GURL BackupUpdateUrl(BackupUpdateReason reason) const;

  // Generates GetHash request URL for retrieving full hashes.
  GURL GetHashUrl(bool is_extended_reporting) const;
  // Generates URL for reporting safe browsing hits for UMA users.

  // Generates GetHashWithApis Pver4 request URL for retrieving full hashes.
  // |request_base64| is the serialized FindFullHashesRequest protocol buffer
  // encoded in base 64.
  GURL GetV4HashUrl(const std::string& request_base64) const;

  // Fills a FindFullHashesRequest protocol buffer for a V4 request.
  // Returns the serialized and base 64 encoded request as a string.
  std::string GetV4HashRequest(const std::vector<SBPrefix>& prefixes,
                               const std::vector<PlatformType>& platforms,
                               ThreatType threat_type);

  // Parses a FindFullHashesResponse protocol buffer and fills the results in
  // |full_hashes| and |negative_cache_duration|. |data| is a serialized
  // FindFullHashes protocol buffer. |negative_cache_duration| is the duration
  // to cache the response for entities that did not match the threat list.
  // Returns true if parsing is successful, false otherwise.
  bool ParseV4HashResponse(const std::string& data_base64,
                           std::vector<SBFullHashResult>* full_hashes,
                           base::TimeDelta* negative_cache_duration);

  // Composes a ChunkUrl based on input string.
  GURL NextChunkUrl(const std::string& input) const;

  // Returns the time for the next update request. If |back_off| is true,
  // the time returned will increment an error count and return the appriate
  // next time (see ScheduleNextUpdate below).
  base::TimeDelta GetNextUpdateInterval(bool back_off);

  // Worker function for calculating GetHash and Update backoff times (in
  // seconds). |multiplier| is doubled for each consecutive error between the
  // 2nd and 5th, and |error_count| is incremented with each call.
  base::TimeDelta GetNextBackOffInterval(size_t* error_count,
                                         size_t* multiplier) const;

  // Worker function for calculating the V4 GetHash backoff times.
  // |multiplier| is doubled for each consecutive error after the
  // first, and |error_count| is incremented with each call.
  static base::TimeDelta GetNextV4BackOffInterval(size_t* error_count,
                                                  size_t* multiplier);

  // Resets the V4 gethash error counter and multiplier.
  void ResetGetHashV4Errors();

  // Manages our update with the next allowable update time. If 'back_off_' is
  // true, we must decrease the frequency of requests of the SafeBrowsing
  // service according to section 5 of the protocol specification.
  // When disable_auto_update_ is set, ScheduleNextUpdate will do nothing.
  // ForceScheduleNextUpdate has to be called to trigger the update.
  void ScheduleNextUpdate(bool back_off);

  // Sends a request for a list of chunks we should download to the SafeBrowsing
  // servers. In order to format this request, we need to send all the chunk
  // numbers for each list that we have to the server. Getting the chunk numbers
  // requires a database query (run on the database thread), and the request
  // is sent upon completion of that query in OnGetChunksComplete.
  void IssueUpdateRequest();

  // Sends a backup request for a list of chunks to download, when the primary
  // update request failed. |reason| specifies why the backup is needed. Unlike
  // the primary IssueUpdateRequest, this does not need to hit the local
  // SafeBrowsing database since the existing chunk numbers are remembered from
  // the primary update request. Returns whether the backup request was issued -
  // this may be false in cases where there is not a prefix specified.
  bool IssueBackupUpdateRequest(BackupUpdateReason reason);

  // Sends a request for a chunk to the SafeBrowsing servers.
  void IssueChunkRequest();

  // Runs the protocol parser on received data and update the
  // SafeBrowsingService with the new content. Returns 'true' on successful
  // parse, 'false' on error.
  bool HandleServiceResponse(const GURL& url, const char* data, size_t length);

  // Updates internal state for each GetHash response error, assuming that the
  // current time is |now|.
  void HandleGetHashError(const base::Time& now);

  // Updates internal state for each GetHash V4 response error, assuming that
  // the current time is |now|.
  void HandleGetHashV4Error(const base::Time& now);

  // Helper function for update completion.
  void UpdateFinished(bool success);
  void UpdateFinished(bool success, bool back_off);

  // A callback that runs if we timeout waiting for a response to an update
  // request. We use this to properly set our update state.
  void UpdateResponseTimeout();

  // Called after the chunks are added to the database.
  void OnAddChunksComplete();

 private:
  // Map of GetHash requests to parameters which created it.
  struct FullHashDetails {
    FullHashDetails();
    FullHashDetails(FullHashCallback callback, bool is_download);
    ~FullHashDetails();

    FullHashCallback callback;
    bool is_download;
  };
  typedef base::hash_map<const net::URLFetcher*, FullHashDetails> HashRequests;

  // The factory that controls the creation of SafeBrowsingProtocolManager.
  // This is used by tests.
  static SBProtocolManagerFactory* factory_;

  // Our delegate.
  SafeBrowsingProtocolManagerDelegate* delegate_;

  // Current active request (in case we need to cancel) for updates or chunks
  // from the SafeBrowsing service. We can only have one of these outstanding
  // at any given time unlike GetHash requests, which are tracked separately.
  scoped_ptr<net::URLFetcher> request_;

  // The kind of request that is currently in progress.
  SafeBrowsingRequestType request_type_;

  // The number of HTTP response errors since the the last successful HTTP
  // response, used for request backoff timing.
  size_t update_error_count_;
  size_t gethash_error_count_;
  size_t gethash_v4_error_count_;

  // Multipliers which double (max == 8) for each error after the second.
  size_t update_back_off_mult_;
  size_t gethash_back_off_mult_;
  size_t gethash_v4_back_off_mult_;

  // Multiplier between 0 and 1 to spread clients over an interval.
  float back_off_fuzz_;

  // The list for which we are make a request.
  std::string list_name_;

  // For managing the next earliest time to query the SafeBrowsing servers for
  // updates.
  base::TimeDelta next_update_interval_;
  base::OneShotTimer update_timer_;

  // timeout_timer_ is used to interrupt update requests which are taking
  // too long.
  base::OneShotTimer timeout_timer_;

  // All chunk requests that need to be made.
  std::deque<ChunkUrl> chunk_request_urls_;

  HashRequests hash_requests_;
  HashRequests v4_hash_requests_;

  // True if the service has been given an add/sub chunk but it hasn't been
  // added to the database yet.
  bool chunk_pending_to_write_;

  // The last time we successfully received an update.
  base::Time last_update_;

  // While in GetHash backoff, we can't make another GetHash until this time.
  base::Time next_gethash_time_;
  // For v4, the next gethash time is set to the backoff time is the last
  // response was an error, or the minimum wait time if the last response was
  // successful.
  base::Time next_gethash_v4_time_;

  // Current product version sent in each request.
  std::string version_;

  // Used for measuring chunk request latency.
  base::Time chunk_request_start_;

  // Tracks the size of each update (in bytes).
  size_t update_size_;

  // The safe browsing client name sent in each request.
  std::string client_name_;

  // A string that is appended to the end of URLs for download, gethash,
  // safebrowsing hits and chunk update requests.
  std::string additional_query_;

  // The context we use to issue network requests.
  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;

  // URL prefix where browser fetches safebrowsing chunk updates, and hashes.
  std::string url_prefix_;

  // Backup URL prefixes for updates.
  std::string backup_url_prefixes_[BACKUP_UPDATE_REASON_MAX];

  // The current reason why the backup update request is happening.
  BackupUpdateReason backup_update_reason_;

  // Data to POST when doing an update.
  std::string update_list_data_;

  // When true, protocol manager will not start an update unless
  // ForceScheduleNextUpdate() is called. This is set for testing purpose.
  bool disable_auto_update_;

  // ID for URLFetchers for testing.
  int url_fetcher_id_;

  // Whether the app is in foreground or background.
  bool app_in_foreground_;

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingProtocolManager);
};

// Interface of a factory to create ProtocolManager.  Useful for tests.
class SBProtocolManagerFactory {
 public:
  SBProtocolManagerFactory() {}
  virtual ~SBProtocolManagerFactory() {}
  virtual SafeBrowsingProtocolManager* CreateProtocolManager(
      SafeBrowsingProtocolManagerDelegate* delegate,
      net::URLRequestContextGetter* request_context_getter,
      const SafeBrowsingProtocolConfig& config) = 0;
 private:
  DISALLOW_COPY_AND_ASSIGN(SBProtocolManagerFactory);
};

// Delegate interface for the SafeBrowsingProtocolManager.
class SafeBrowsingProtocolManagerDelegate {
 public:
  typedef base::Callback<void(
      const std::vector<SBListChunkRanges>&, /* List of chunks */
      bool,                                  /* database_error */
      bool                                   /* is_extended_reporting */
      )> GetChunksCallback;
  typedef base::Callback<void(void)> AddChunksCallback;

  virtual ~SafeBrowsingProtocolManagerDelegate();

  // |UpdateStarted()| is called just before the SafeBrowsing update protocol
  // has begun.
  virtual void UpdateStarted() = 0;

  // |UpdateFinished()| is called just after the SafeBrowsing update protocol
  // has completed.
  virtual void UpdateFinished(bool success) = 0;

  // Wipe out the local database. The SafeBrowsing server can request this.
  virtual void ResetDatabase() = 0;

  // Retrieve all the local database chunks, and invoke |callback| with the
  // results. The SafeBrowsingProtocolManagerDelegate must only invoke the
  // callback if the SafeBrowsingProtocolManager is still alive. Only one call
  // may be made to GetChunks at a time.
  virtual void GetChunks(GetChunksCallback callback) = 0;

  // Add new chunks to the database. Invokes |callback| when complete, but must
  // call at a later time.
  virtual void AddChunks(
      const std::string& list,
      scoped_ptr<std::vector<scoped_ptr<SBChunkData>>> chunks,
      AddChunksCallback callback) = 0;

  // Delete chunks from the database.
  virtual void DeleteChunks(
      scoped_ptr<std::vector<SBChunkDelete> > chunk_deletes) = 0;
};

}  // namespace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_PROTOCOL_MANAGER_H_
