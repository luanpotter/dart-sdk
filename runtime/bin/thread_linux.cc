// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/globals.h"
#if (defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID)) &&          \
    !defined(DART_USE_ABSL)

#include "bin/thread.h"
#include "bin/thread_linux.h"

#include <errno.h>         // NOLINT
#include <sys/resource.h>  // NOLINT
#include <sys/time.h>      // NOLINT

#include "platform/assert.h"
#include "platform/utils.h"

namespace dart {
namespace bin {

#define VALIDATE_PTHREAD_RESULT(result)                                        \
  if (result != 0) {                                                           \
    const int kBufferSize = 1024;                                              \
    char error_buf[kBufferSize];                                               \
    FATAL("pthread error: %d (%s)", result,                                    \
          Utils::StrError(result, error_buf, kBufferSize));                    \
  }

#ifdef DEBUG
#define RETURN_ON_PTHREAD_FAILURE(result)                                      \
  if (result != 0) {                                                           \
    const int kBufferSize = 1024;                                              \
    char error_buf[kBufferSize];                                               \
    fprintf(stderr, "%s:%d: pthread error: %d (%s)\n", __FILE__, __LINE__,     \
            result, Utils::StrError(result, error_buf, kBufferSize));          \
    return result;                                                             \
  }
#else
#define RETURN_ON_PTHREAD_FAILURE(result)                                      \
  if (result != 0) {                                                           \
    return result;                                                             \
  }
#endif

static void ComputeTimeSpecMicros(struct timespec* ts, int64_t micros) {
  int64_t secs = micros / kMicrosecondsPerSecond;
  int64_t nanos =
      (micros - (secs * kMicrosecondsPerSecond)) * kNanosecondsPerMicrosecond;
  int result = clock_gettime(CLOCK_MONOTONIC, ts);
  ASSERT(result == 0);
  ts->tv_sec += secs;
  ts->tv_nsec += nanos;
  if (ts->tv_nsec >= kNanosecondsPerSecond) {
    ts->tv_sec += 1;
    ts->tv_nsec -= kNanosecondsPerSecond;
  }
}

class ThreadStartData {
 public:
  ThreadStartData(const char* name,
                  Thread::ThreadStartFunction function,
                  uword parameter)
      : name_(name), function_(function), parameter_(parameter) {}

  const char* name() const { return name_; }
  Thread::ThreadStartFunction function() const { return function_; }
  uword parameter() const { return parameter_; }

 private:
  const char* name_;
  Thread::ThreadStartFunction function_;
  uword parameter_;

  DISALLOW_COPY_AND_ASSIGN(ThreadStartData);
};

// Dispatch to the thread start function provided by the caller. This trampoline
// is used to ensure that the thread is properly destroyed if the thread just
// exits.
static void* ThreadStart(void* data_ptr) {
  ThreadStartData* data = reinterpret_cast<ThreadStartData*>(data_ptr);

  const char* name = data->name();
  Thread::ThreadStartFunction function = data->function();
  uword parameter = data->parameter();
  delete data;

  // Set the thread name. There is 16 bytes limit on the name (including \0).
  // pthread_setname_np ignores names that are too long rather than truncating.
  char truncated_name[16];
  snprintf(truncated_name, sizeof(truncated_name), "%s", name);
  pthread_setname_np(pthread_self(), truncated_name);

  // Call the supplied thread start function handing it its parameters.
  function(parameter);

  return nullptr;
}

int Thread::Start(const char* name,
                  ThreadStartFunction function,
                  uword parameter) {
  pthread_attr_t attr;
  int result = pthread_attr_init(&attr);
  RETURN_ON_PTHREAD_FAILURE(result);

  result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  RETURN_ON_PTHREAD_FAILURE(result);

  result = pthread_attr_setstacksize(&attr, Thread::GetMaxStackSize());
  RETURN_ON_PTHREAD_FAILURE(result);

  ThreadStartData* data = new ThreadStartData(name, function, parameter);

  pthread_t tid;
  result = pthread_create(&tid, &attr, ThreadStart, data);
  RETURN_ON_PTHREAD_FAILURE(result);

  result = pthread_attr_destroy(&attr);
  RETURN_ON_PTHREAD_FAILURE(result);

  return 0;
}

const ThreadId Thread::kInvalidThreadId = static_cast<ThreadId>(0);

intptr_t Thread::GetMaxStackSize() {
  const int kStackSize = (128 * kWordSize * KB);
  return kStackSize;
}

ThreadId Thread::GetCurrentThreadId() {
  return pthread_self();
}

bool Thread::Compare(ThreadId a, ThreadId b) {
  return (pthread_equal(a, b) != 0);
}

Mutex::Mutex() {
  pthread_mutexattr_t attr;
  int result = pthread_mutexattr_init(&attr);
  VALIDATE_PTHREAD_RESULT(result);

#if defined(DEBUG)
  result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  VALIDATE_PTHREAD_RESULT(result);
#endif  // defined(DEBUG)

  result = pthread_mutex_init(data_.mutex(), &attr);
  // Verify that creating a pthread_mutex succeeded.
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_mutexattr_destroy(&attr);
  VALIDATE_PTHREAD_RESULT(result);
}

Mutex::~Mutex() {
  int result = pthread_mutex_destroy(data_.mutex());
  // Verify that the pthread_mutex was destroyed.
  VALIDATE_PTHREAD_RESULT(result);
}

void Mutex::Lock() {
  int result = pthread_mutex_lock(data_.mutex());
  // Specifically check for dead lock to help debugging.
  ASSERT(result != EDEADLK);
  ASSERT(result == 0);  // Verify no other errors.
  // TODO(iposva): Do we need to track lock owners?
}

bool Mutex::TryLock() {
  int result = pthread_mutex_trylock(data_.mutex());
  // Return false if the lock is busy and locking failed.
  if (result == EBUSY) {
    return false;
  }
  ASSERT(result == 0);  // Verify no other errors.
  // TODO(iposva): Do we need to track lock owners?
  return true;
}

void Mutex::Unlock() {
  // TODO(iposva): Do we need to track lock owners?
  int result = pthread_mutex_unlock(data_.mutex());
  // Specifically check for wrong thread unlocking to aid debugging.
  ASSERT(result != EPERM);
  ASSERT(result == 0);  // Verify no other errors.
}

ConditionVariable::ConditionVariable() {
  pthread_condattr_t cond_attr;
  int result = pthread_condattr_init(&cond_attr);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_cond_init(data_.cond(), &cond_attr);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_condattr_destroy(&cond_attr);
  VALIDATE_PTHREAD_RESULT(result);
}

ConditionVariable::~ConditionVariable() {
  int result = pthread_cond_destroy(data_.cond());
  VALIDATE_PTHREAD_RESULT(result);
}

void ConditionVariable::Wait(Mutex* mutex) {
  int result = pthread_cond_wait(data_.cond(), mutex->data_.mutex());
  VALIDATE_PTHREAD_RESULT(result);
}

void ConditionVariable::Notify() {
  // TODO(iposva): Do we need to track lock owners?
  int result = pthread_cond_signal(data_.cond());
  VALIDATE_PTHREAD_RESULT(result);
}

Monitor::Monitor() {
  pthread_mutexattr_t mutex_attr;
  int result = pthread_mutexattr_init(&mutex_attr);
  VALIDATE_PTHREAD_RESULT(result);

#if defined(DEBUG)
  result = pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK);
  VALIDATE_PTHREAD_RESULT(result);
#endif  // defined(DEBUG)

  result = pthread_mutex_init(data_.mutex(), &mutex_attr);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_mutexattr_destroy(&mutex_attr);
  VALIDATE_PTHREAD_RESULT(result);

  pthread_condattr_t cond_attr;
  result = pthread_condattr_init(&cond_attr);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_cond_init(data_.cond(), &cond_attr);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_condattr_destroy(&cond_attr);
  VALIDATE_PTHREAD_RESULT(result);
}

Monitor::~Monitor() {
  int result = pthread_mutex_destroy(data_.mutex());
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_cond_destroy(data_.cond());
  VALIDATE_PTHREAD_RESULT(result);
}

void Monitor::Enter() {
  int result = pthread_mutex_lock(data_.mutex());
  VALIDATE_PTHREAD_RESULT(result);
  // TODO(iposva): Do we need to track lock owners?
}

void Monitor::Exit() {
  // TODO(iposva): Do we need to track lock owners?
  int result = pthread_mutex_unlock(data_.mutex());
  VALIDATE_PTHREAD_RESULT(result);
}

Monitor::WaitResult Monitor::Wait(int64_t millis) {
  return WaitMicros(millis * kMicrosecondsPerMillisecond);
}

Monitor::WaitResult Monitor::WaitMicros(int64_t micros) {
  // TODO(iposva): Do we need to track lock owners?
  Monitor::WaitResult retval = kNotified;
  if (micros == kNoTimeout) {
    // Wait forever.
    int result = pthread_cond_wait(data_.cond(), data_.mutex());
    VALIDATE_PTHREAD_RESULT(result);
  } else {
    struct timespec ts;
    ComputeTimeSpecMicros(&ts, micros);
    int result = pthread_cond_timedwait(data_.cond(), data_.mutex(), &ts);
    ASSERT((result == 0) || (result == ETIMEDOUT));
    if (result == ETIMEDOUT) {
      retval = kTimedOut;
    }
  }
  return retval;
}

void Monitor::Notify() {
  // TODO(iposva): Do we need to track lock owners?
  int result = pthread_cond_signal(data_.cond());
  VALIDATE_PTHREAD_RESULT(result);
}

void Monitor::NotifyAll() {
  // TODO(iposva): Do we need to track lock owners?
  int result = pthread_cond_broadcast(data_.cond());
  VALIDATE_PTHREAD_RESULT(result);
}

}  // namespace bin
}  // namespace dart

#endif  // (defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID)) &&   \
        // !defined(DART_USE_ABSL)
