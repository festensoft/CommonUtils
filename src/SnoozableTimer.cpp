#include "SnoozableTimer.h"
#include <chrono>
#include <utility>

namespace CommonUtils {
SnoozableTimer::SnoozableTimer(std::function<void ()> function, int snoozePeriodMs)
    : _function(std::move(function))
    , _snoozePeriodMs(snoozePeriodMs)
{

}

SnoozableTimer::~SnoozableTimer()
{
    Stop();
}

void SnoozableTimer::Start()
{
    if (_isRunning) return;

    _executionTime = std::chrono::high_resolution_clock::now() +
                      std::chrono::milliseconds(_snoozePeriodMs);

    _isRunning = true;
    // _thread itself (not just _isRunning/_executionTime) needs its own
    // lock too -- its member functions aren't safe to call concurrently
    // from multiple threads. Uses _threadMutex, not _mutex -- see its
    // declaration in the header.
    const std::lock_guard<std::mutex> threadLock(_threadMutex);
    _thread = std::thread([this]()
    {
        std::unique_lock<std::mutex> lock(_mutex);

        while (_isRunning)
        {
            // Wait until execution time or until notified
            const auto deadline = _executionTime;
            if (_cv.wait_until(lock, deadline) == std::cv_status::timeout && _isRunning)
            {
                lock.unlock();
                // Never hold _mutex while invoking user code -- _function()
                // may legally call stop()/snooze() on this same timer.
                _function();
                lock.lock();
                // Only park for a long time if nothing (e.g. _function()
                // itself, via snooze()) already scheduled a new deadline
                // while we were running _function() without the lock held.
                // This should prevent reexecution until we hit the snooze!
                if (_executionTime == deadline)
                {
                    _executionTime = std::chrono::high_resolution_clock::now() +
                                     std::chrono::hours(87600);
                }
            }
        }
    });
}

void SnoozableTimer::Stop()
{
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _isRunning = false;
    }

    _cv.notify_all();

    // _thread's own operations need a lock too, not just _isRunning --
    // this is _threadMutex, not _mutex: the worker thread never touches
    // _threadMutex, so holding it across the blocking join() call below
    // is safe. Holding _mutex across join() instead would deadlock -- the
    // worker's trailing cleanup (after wait_until()/_function(), see
    // start()) needs to re-lock _mutex to finish and return, which it
    // could never do while this call sits blocked in join() holding that
    // same lock.
    const std::lock_guard<std::mutex> threadLock(_threadMutex);

    if (!_thread.joinable())
    {
        return;
    }

    if (_thread.get_id() == std::this_thread::get_id())
    {
        // Called from within our own callback (self-stop). A thread can't
        // join itself -- detach so the handle is safe to reassign on the
        // next start() instead of leaking/asserting.
        _thread.detach();
    }
    else
    {
        _thread.join();
    }
}

void SnoozableTimer::Snooze()
{
    const std::lock_guard<std::mutex> lock(_mutex);
    auto lcTimeNow = std::chrono::high_resolution_clock::now() +
            std::chrono::milliseconds(_snoozePeriodMs);
    _executionTime = lcTimeNow;
    _cv.notify_all(); // Notify to re-evaluate the wait condition
}

void SnoozableTimer::UpdateSnoozePeriod(int anSnoozePeriodMs)
{
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _snoozePeriodMs = anSnoozePeriodMs;
    }
    Snooze();
}

} // end namespace CommonUtils
