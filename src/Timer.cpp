#include "Timer.h"

namespace CommonUtils
{

Timer::Timer() : _isRunning(false)
{

}

void Timer::StartOneShot(const std::function<void ()> &func, unsigned int interval)
{
    Stop();

    _isRunning = true;
    const unsigned long long myGeneration = _generation.load();
    // _thread itself (not just _isRunning/_generation) needs its own lock
    // too -- its member functions aren't safe to call concurrently from
    // multiple threads (e.g. an external stop() call racing this
    // assignment, or a self-restart's stop() reading joinable()/get_id()
    // while another thread reassigns it). ThreadSanitizer caught this as
    // a real race. Uses _threadMutex, not _mutex -- see its declaration.
    const std::lock_guard<std::mutex> threadLock(_threadMutex);
    _thread = std::thread([this, func, interval, myGeneration]()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        const bool cancelled = _cv.wait_for(lock, std::chrono::milliseconds(interval),
            [this, myGeneration]() { return !_isRunning || _generation != myGeneration; });
        lock.unlock();

        if (cancelled)
        {
            return;
        }

        // Never hold _mutex while invoking user code -- func() may legally
        // call stop()/startOneShot()/startPeriodic() on this same Timer.
        func();

        lock.lock();
        // Only clear _isRunning if nothing (e.g. func() itself) has already
        // started a newer run; otherwise this would incorrectly cancel it.
        if (_generation == myGeneration)
        {
            _isRunning = false;
        }
    });
}

void Timer::StartPeriodic(const std::function<void ()> &func, unsigned int interval)
{
    Stop();

    _isRunning = true;
    const unsigned long long myGeneration = _generation.load();
    // See the comment in startOneShot() -- _thread itself needs its own
    // lock too (_threadMutex, not _mutex).
    const std::lock_guard<std::mutex> threadLock(_threadMutex);
    _thread = std::thread([this, func, interval, myGeneration]()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            const bool stopped = _cv.wait_for(lock, std::chrono::milliseconds(interval),
                [this, myGeneration]() { return !_isRunning || _generation != myGeneration; });
            lock.unlock();

            if (stopped)
            {
                return;
            }

            // Never hold _mutex while invoking user code -- func() may
            // legally call stop()/startOneShot()/startPeriodic() on this
            // same Timer (e.g. to end its own periodic execution).
            func();
        }
    });
}

void Timer::Stop()
{
    {
        const std::lock_guard<std::mutex> lock(_mutex);
        _isRunning = false;
        ++_generation;
    }
    _cv.notify_all();

    // _thread's own operations (joinable/get_id/detach/join) need a lock
    // too, not just _isRunning/_generation -- std::thread's member
    // functions aren't safe to call concurrently on the same object from
    // multiple threads, e.g. an external stop() racing a self-restart's
    // internal stop() call. This is _threadMutex, not _mutex: the worker
    // thread never touches _threadMutex, so holding it across the
    // blocking join() call below is safe. Holding _mutex across join()
    // instead would deadlock -- the worker's trailing cleanup (after
    // wait_for()/func(), see startOneShot()/startPeriodic()) needs to
    // re-lock _mutex to finish and return, which it could never do while
    // this call sits blocked in join() holding that same lock.
    const std::lock_guard<std::mutex> threadLock(_threadMutex);

    if (!_thread.joinable())
    {
        return;
    }

    if (_thread.get_id() == std::this_thread::get_id())
    {
        // Called from within our own callback (self-stop or self-restart).
        // A thread can't join itself -- detach so the handle is safe to
        // reassign afterward instead of leaking/asserting on the next
        // startOneShot()/startPeriodic() call.
        _thread.detach();
    }
    else
    {
        _thread.join();
    }
}

Timer::~Timer()
{
    Stop();
}

}
