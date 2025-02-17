// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_BIN_THREAD_LINUX_H_
#define RUNTIME_BIN_THREAD_LINUX_H_

#if !defined(RUNTIME_BIN_THREAD_H_)
#error Do not include thread_linux.h directly; use thread.h instead.
#endif

#include <pthread.h>

#include "platform/assert.h"
#include "platform/globals.h"

namespace dart {
namespace bin {

typedef pthread_t ThreadId;

class MutexData {
 private:
  MutexData() {}
  ~MutexData() {}

  pthread_mutex_t* mutex() { return &mutex_; }

  pthread_mutex_t mutex_;

  friend class Mutex;
  friend class ConditionVariable;

  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(MutexData);
};

class ConditionVariableData {
 private:
  ConditionVariableData() {}
  ~ConditionVariableData() {}

  pthread_cond_t* cond() { return &cond_; }
  pthread_cond_t cond_;

  friend class ConditionVariable;

  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(ConditionVariableData);
};

class MonitorData {
 private:
  MonitorData() {}
  ~MonitorData() {}

  pthread_mutex_t* mutex() { return &mutex_; }
  pthread_cond_t* cond() { return &cond_; }

  pthread_mutex_t mutex_;
  pthread_cond_t cond_;

  friend class Monitor;

  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(MonitorData);
};

}  // namespace bin
}  // namespace dart

#endif  // RUNTIME_BIN_THREAD_LINUX_H_
