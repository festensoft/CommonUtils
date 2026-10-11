#include "DataHandler.h"
#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

TEST(DataHandlerTest, SignalDataNotifiesListeners)
{
    // handler is declared last (and so, per reverse-declaration-order
    // destruction, destroyed FIRST) deliberately: its destructor joins the
    // worker thread, which is what actually guarantees no listener
    // callback can still be touching cv/mtx (e.g. mid-notify_one()) by the
    // time they're destroyed below. Declaring handler first was a genuine
    // data race, caught by ThreadSanitizer: cv.wait_for()'s predicate
    // becoming true only means the last notify_one() call was observed,
    // not that the notifying thread has fully returned from it.
    std::atomic<int> count{0};
    std::mutex mtx;
    std::condition_variable cv;
    CommonUtils::DataHandler<int> handler;

    auto listener1 = [&](const int& data) {
        EXPECT_EQ(data, 42);
        count.fetch_add(1);
        cv.notify_one();
    };

    auto listener2 = [&](const int& data) {
        EXPECT_EQ(data, 42);
        count.fetch_add(1);
        cv.notify_one();
    };

    handler.RegisterListener(listener1);
    handler.RegisterListener(listener2);

    handler.SignalData(42);

    // Wait up to 500ms for both listeners to be called
    std::unique_lock<std::mutex> lk(mtx);
    const bool ok = cv.wait_for(lk, std::chrono::milliseconds(500), [&]{ return count.load() >= 2; });
    EXPECT_TRUE(ok);
}

TEST(DataHandlerTest, ExpiredListenersAreRemoved)
{
    // handler declared last / destroyed first -- see the comment in
    // SignalDataNotifiesListeners above.
    std::atomic<int> count{0};
    std::mutex mtx;
    std::condition_variable cv;
    CommonUtils::DataHandler<int> handler;

    auto listener1 = [&](const int& data) {
        EXPECT_EQ(data, 42);
        count.fetch_add(1);
        cv.notify_one();
    };

    auto regId1 = handler.RegisterListener(listener1);

    auto listener2 = [&](const int& data) {
        EXPECT_EQ(data, 42);
        count.fetch_add(1);
        cv.notify_one();
    };

    auto regId2 = handler.RegisterListener(listener2);

    handler.SignalData(42);

    // Wait up to 500ms for both listeners to be called
    std::unique_lock<std::mutex> lk(mtx);
    const bool ok = cv.wait_for(lk, std::chrono::milliseconds(500), [&]{ return count.load() >= 2; });
    EXPECT_TRUE(ok);

    // There should be two listeners registered
    EXPECT_EQ(handler.WatermarkInfo().first, 2U);

    handler.UnregisterListener(regId1);
    EXPECT_EQ(handler.WatermarkInfo().first, 1U);

    handler.UnregisterListener(regId2);
    EXPECT_EQ(handler.WatermarkInfo().first, 0U);

}

// Regression test: a listener unregistering itself must not deadlock.
// notifyListeners() previously held _listenersMutex for the whole
// iteration, so a listener calling unregisterListener() (or
// registerListener()/watermarkInfo()) on the same handler would re-lock a
// non-recursive mutex already held by the worker thread.
TEST(DataHandlerTest, ListenerCanUnregisterItself)
{
    // handler declared last / destroyed first -- see the comment in
    // SignalDataNotifiesListeners above. The trailing sleep_for() in this
    // test happens to make the race very unlikely to manifest in
    // practice, but the ordering is still formally unsafe without this.
    std::atomic<int> callCount{0};
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    CommonUtils::DataHandler<int> handler;

    int id = -1;
    id = handler.RegisterListener([&](const int&)
    {
        callCount.fetch_add(1);
        handler.UnregisterListener(id); // must not deadlock

        {
            const std::lock_guard<std::mutex> lock(mtx);
            done = true;
        }
        cv.notify_one();
    });

    handler.SignalData(1);

    // If unregisterListener() deadlocks above, this wait times out rather
    // than hanging the whole test binary.
    std::unique_lock<std::mutex> lk(mtx);
    const bool ok = cv.wait_for(lk, std::chrono::milliseconds(500), [&] { return done; });
    lk.unlock();
    ASSERT_TRUE(ok);
    EXPECT_EQ(callCount.load(), 1);
    EXPECT_EQ(handler.WatermarkInfo().first, 0U);

    // The now-unregistered listener must not be called again.
    handler.SignalData(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(callCount.load(), 1);
}

// Regression test: watermarkInfo() previously read _dataQueue under
// _listenersMutex instead of the _cvMutex that actually guards it
// elsewhere (signalData()/processData()) -- a genuine data race. Hammer
// both concurrently; the assertion is simply that this doesn't crash or
// corrupt state (no sanitizer available on this toolchain to prove the
// race is gone directly, so this is a best-effort stress test).
TEST(DataHandlerTest, ConcurrentSignalAndWatermarkDoNotCrash)
{
    CommonUtils::DataHandler<int> handler;
    std::atomic<bool> stop{false};

    std::thread producer([&handler, &stop]()
    {
        int i = 0;
        while (!stop)
        {
            handler.SignalData(i++);
        }
    });

    for (int i = 0; i < 2000; ++i)
    {
        const auto info = handler.WatermarkInfo();
        EXPECT_LE(info.first, 1U); // at most the one listener registered below
    }

    handler.RegisterListener([](const int&) {});
    for (int i = 0; i < 2000; ++i)
    {
        (void)handler.WatermarkInfo();
    }

    stop = true;
    producer.join();
}

// Regression test: the destructor previously set _stopFlag and called
// notify_all() without holding _cvMutex -- a textbook condition_variable
// lost-wakeup race (an atomic flag alone doesn't establish the
// happens-before relationship wait() relies on). Under adverse timing this
// could hang _workerThread.join() forever. Repeated construct/destroy
// cycles with data still queued at destruction time exercise the exact
// shutdown path where the race could occur; the test simply must not hang.
TEST(DataHandlerTest, DestructorCompletesPromptlyWithPendingData)
{
    for (int iter = 0; iter < 200; ++iter)
    {
        CommonUtils::DataHandler<int> handler;
        handler.RegisterListener([](const int&)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        });
        for (int i = 0; i < 5; ++i)
        {
            handler.SignalData(i);
        }
        // Destructor runs here, possibly while the worker thread is still
        // mid-wait or mid-processing -- must return promptly, not hang.
    }
    SUCCEED();
}