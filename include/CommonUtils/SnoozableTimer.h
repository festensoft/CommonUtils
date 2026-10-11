#ifndef COMMONUTILS_SNOOZABLETIMER_H_
#define COMMONUTILS_SNOOZABLETIMER_H_

// System headers
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace CommonUtils {
/**
 * @class SnoozableTimer
 * @brief Timer that executes a callback after a snooze period, with the
 *        ability to extend (snooze) the deadline.
 *
 * Once started, the timer counts down for the configured snooze period.
 * Calling snooze() resets the deadline to NOW + snoozePeriod.
 *
 * The callback is invoked without holding the timer's internal lock, so it
 * is safe for the callback to call stop() or snooze() on this same timer
 * (e.g. to end itself, or to reschedule itself for another period).
 *
 * Not safe to call start()/stop()/snooze()/updateSnoozePeriod()
 * concurrently from multiple external threads on the same instance without
 * additional synchronization -- only self-calls from within the timer's
 * own callback are specifically supported.
 */
class SnoozableTimer
{
public:
   /**
    * @brief Construct a SnoozableTimer.
    *
    * @param function       Callback to invoke when the timer expires.
    * @param snoozePeriodMs Initial snooze period in milliseconds.
    */
   SnoozableTimer(std::function<void()> function, int snoozePeriodMs);

   /**
    * @brief Destroy the timer, stopping it if running.
    */
   ~SnoozableTimer();

   // Non-copyable.
   SnoozableTimer(const SnoozableTimer&) = delete;
   SnoozableTimer& operator=(const SnoozableTimer&) = delete;

   /**
    * @brief Start the timer.
    */
   void Start();

   /**
    * @brief Stop the timer.
    */
   void Stop();

   /**
    * @brief Reset the deadline to NOW + snoozePeriod.
    */
   void Snooze();

   /**
    * @brief Update the snooze period and immediately snooze with the new
    *        duration.
    *
    * @param snoozePeriodMs New snooze period in milliseconds.
    */
   void UpdateSnoozePeriod(int snoozePeriodMs);

private:
   std::function<void()> _function;
   std::chrono::high_resolution_clock::time_point _executionTime;
   mutable std::mutex _mutex;
   std::condition_variable _cv;
   std::thread _thread;
   // Guards _thread's own state (joinable()/get_id()/detach()/join()/
   // assignment) -- separate from _mutex, which the worker thread needs
   // to finish its own execution. Holding _mutex across the blocking
   // join() call in stop() would deadlock: the worker's trailing cleanup
   // (after wait_until()/_function()) re-locks _mutex to finish and
   // return, which it could never do while stop() holds it waiting on
   // join(). _threadMutex has no such conflict since the worker lambda
   // never touches it.
   mutable std::mutex _threadMutex;
   int _snoozePeriodMs;
   std::atomic<bool> _isRunning{false};
};

}
#endif // COMMONUTILS_SNOOZABLETIMER_H_
