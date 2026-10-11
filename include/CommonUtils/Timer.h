// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef COMMONUTILS_TIMER_H_
#define COMMONUTILS_TIMER_H_

// System headers
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace CommonUtils
{

/**
 * @class Timer
 * @brief Simple timer that can execute a function after a specified amount
 *        of time, or periodically.  Designed for easy cancellation and
 *        destruction.
 *
 * The callback is invoked without holding the Timer's internal lock, so it
 * is safe for the callback to call stop(), startOneShot(), or
 * startPeriodic() on this same Timer (e.g. a periodic callback stopping
 * itself after N firings, or a one-shot callback re-arming itself).
 *
 * Not safe to call startOneShot()/startPeriodic()/stop() concurrently from
 * multiple external threads on the same Timer instance without additional
 * synchronization -- only self-calls from within the timer's own callback
 * are specifically supported.
 */
class Timer
{
public:
   /**
    * @brief Construct a cancelable timer.
    */
   Timer();

   /**
    * @brief Destroy the timer, cancelling any pending operation.
    */
   ~Timer();

   // Non-copyable.
   Timer(const Timer&) = delete;
   Timer& operator=(const Timer&) = delete;

   /**
    * @brief Execute a function once after a specified delay.
    *
    * @param func     Function to execute.
    * @param interval Delay before execution, in milliseconds.
    */
   void StartOneShot(const std::function<void()>& func, unsigned int interval);

   /**
    * @brief Execute a function periodically.
    *
    * @param func     Function to execute.
    * @param interval Period between executions, in milliseconds.
    */
   void StartPeriodic(const std::function<void()>& func, unsigned int interval);

   /**
    * @brief Cancel the timer.
    */
   void Stop();

private:
   std::atomic<bool> _isRunning;
   // Bumped every time a run is retired (cancelled, superseded, or a fresh
   // start). Lets a still-executing callback tell whether it's been
   // superseded by a newer run (e.g. it restarted itself) so it doesn't
   // clobber that newer run's state on the way out. Always read/written
   // while holding _mutex.
   std::atomic<unsigned long long> _generation{0};
   std::thread _thread;
   // Guards _thread's own state (joinable()/get_id()/detach()/join()/
   // assignment) -- separate from _mutex below, which the worker thread
   // needs to finish its own execution. Holding _mutex across the
   // blocking join() call in stop() would deadlock: the worker's trailing
   // cleanup (after wait_for()/func()) re-locks _mutex to finish and
   // return, which it could never do while stop() holds it waiting on
   // join(). _threadMutex has no such conflict since the worker lambda
   // never touches it.
   std::mutex _threadMutex;
   std::mutex _mutex;
   std::condition_variable _cv;
};

} // namespace CommonUtils

#endif // COMMONUTILS_TIMER_H_
