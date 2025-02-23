// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_BACKOFF_TIMER_H_
#define REMOTING_HOST_BACKOFF_TIMER_H_

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/timer/timer.h"
#include "net/base/backoff_entry.h"

namespace remoting {

// An object similar to a base::Timer with exponential backoff.
class BackoffTimer {
 public:
  BackoffTimer();
  ~BackoffTimer();

  // Invokes |user_task| at intervals specified by |delay|, and
  // increasing up to |max_delay|.  Always invokes |user_task| before
  // the first scheduled delay.
  void Start(const tracked_objects::Location& posted_from,
             base::TimeDelta delay,
             base::TimeDelta max_delay,
             const base::Closure& user_task);

  // Prevents the user task from being invoked again.
  void Stop();

  // Returns true if the user task may be invoked in the future.
  bool IsRunning() const { return backoff_entry_; }

  void SetTimerForTest(scoped_ptr<base::Timer> timer);

 private:
  void StartTimer();
  void OnTimerFired();

  scoped_ptr<base::Timer> timer_;
  base::Closure user_task_;
  tracked_objects::Location posted_from_;
  net::BackoffEntry::Policy backoff_policy_ = {};
  scoped_ptr<net::BackoffEntry> backoff_entry_;

  DISALLOW_COPY_AND_ASSIGN(BackoffTimer);
};

}  // namespace remoting

#endif  // REMOTING_HOST_BACKOFF_TIMER_H_
