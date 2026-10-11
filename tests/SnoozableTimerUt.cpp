#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <functional>
#include "SnoozableTimer.h"

class SnoozableTimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        snExecutionCount = 0;
    }

    // atomic: written from the timer's worker thread (via _incrementCount,
    // invoked as the SnoozableTimer callback), read from the test's main
    // thread in every EXPECT_EQ below. A plain int here is a real data
    // race (caught by ThreadSanitizer) even though it "worked" in practice
    // on x86 -- relying on an intervening sleep_for() is not a substitute
    // for actual synchronization.
    static std::atomic<int> snExecutionCount;
    std::function<void()> _incrementCount = [&]() { snExecutionCount++; };
};

std::atomic<int> SnoozableTimerTest::snExecutionCount{0};

// Test basic execution timing
TEST_F(SnoozableTimerTest, ExecutesAtSpecifiedTime) {
    auto start = std::chrono::high_resolution_clock::now();
    auto execTime = start + std::chrono::milliseconds(500);

    CommonUtils::SnoozableTimer executor(_incrementCount, 500);
    executor.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    executor.Stop();

    EXPECT_EQ(snExecutionCount, 1);
    auto now = std::chrono::high_resolution_clock::now();
    EXPECT_GE(now, execTime);
}

// Test adding time before execution
TEST_F(SnoozableTimerTest, AddTimeBeforeExecution) {

    CommonUtils::SnoozableTimer executor(_incrementCount, 500);
    executor.Start();

    executor.UpdateSnoozePeriod(500);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    EXPECT_EQ(snExecutionCount, 0); // Shouldn't execute yet

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    executor.Stop();

    EXPECT_EQ(snExecutionCount, 1);
}

// Test immediate execution (edge case: time in past)
TEST_F(SnoozableTimerTest, ExecutesImmediatelyForPastTime) {

    CommonUtils::SnoozableTimer executor(_incrementCount, -500);
    executor.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    executor.Stop();

    EXPECT_EQ(snExecutionCount, 1);
}

// Test multiple starts don't create multiple threads
TEST_F(SnoozableTimerTest, MultipleStartsAreIdempotent) {

    CommonUtils::SnoozableTimer executor(_incrementCount, 500);
    executor.Start();
    executor.Start(); // Second start should do nothing

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    executor.Stop();

    EXPECT_EQ(snExecutionCount, 1); // Should only execute once
}

// Test stop before execution
TEST_F(SnoozableTimerTest, StopBeforeExecution) {

   CommonUtils:: SnoozableTimer executor(_incrementCount, 1000);
    executor.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    executor.Stop();

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_EQ(snExecutionCount, 0);
}

// Test adding negative time
TEST_F(SnoozableTimerTest, AddNegativeTime) {

    CommonUtils::SnoozableTimer executor(_incrementCount, 1000);
    executor.Start();

    executor.UpdateSnoozePeriod(-500);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    EXPECT_EQ(snExecutionCount, 1);
    executor.Stop();
}

// Test destructor cleanup
TEST_F(SnoozableTimerTest, DestructorCleansUp) {
    {
        CommonUtils::SnoozableTimer executor(_incrementCount, 1000);
        executor.Start();
    } // Destructor called here

    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_EQ(snExecutionCount, 0); // Shouldn't execute after destruction
}

// Test zero duration
TEST_F(SnoozableTimerTest, ZeroDuration) {
    CommonUtils::SnoozableTimer executor(_incrementCount, 0);
    executor.Start();

    // _executionTime is captured as now() before the worker thread is even
    // spawned (see SnoozableTimer::start()), so firing is gated purely by
    // OS thread-creation/scheduling latency, not any real countdown. That's
    // normally sub-millisecond but can spike well past 10ms under load
    // (e.g. right after other tests in this binary spun up many threads),
    // so this needs a generous margin to avoid flaking rather than a tight
    // one tuned for an idle machine.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(snExecutionCount, 1);
    executor.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(snExecutionCount, 1);
}

// Test basic snooze
TEST_F(SnoozableTimerTest, SingleSnooze) {
    CommonUtils::SnoozableTimer executor(_incrementCount, 100);
    executor.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    executor.Snooze();
    EXPECT_EQ(snExecutionCount, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    EXPECT_EQ(snExecutionCount, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(snExecutionCount, 1);
    executor.Stop();
    EXPECT_EQ(snExecutionCount, 1);
}

// test Multple snoozes
TEST_F(SnoozableTimerTest, MultiSnooze) {
    CommonUtils::SnoozableTimer executor(_incrementCount, 100);
    executor.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    EXPECT_EQ(snExecutionCount, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(snExecutionCount, 1);
    executor.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    EXPECT_EQ(snExecutionCount, 1);
}

// test Multple snoozes
TEST_F(SnoozableTimerTest, SnoozeTimeoutSnoozeAgain) {
    CommonUtils::SnoozableTimer executor(_incrementCount, 100);
    executor.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(snExecutionCount, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    EXPECT_EQ(snExecutionCount, 1);

    executor.Snooze();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(snExecutionCount, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(snExecutionCount, 2);
    executor.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    EXPECT_EQ(snExecutionCount, 2);
}

// Regression test: a callback stopping the timer it belongs to must not
// crash. stop() previously called join() on the thread it was invoked
// from -- a self-join, which is undefined behavior (std::terminate() in
// practice, since the resulting exception escapes the thread's entry
// function uncaught).
TEST_F(SnoozableTimerTest, CallbackCanStopItself) {
    CommonUtils::SnoozableTimer* self = nullptr;
    CommonUtils::SnoozableTimer timer([&self]()
    {
        snExecutionCount++;
        self->Stop();
    }, 20);
    self = &timer;

    timer.Start();

    // If the callback's stop() call crashes or deadlocks, this test aborts
    // the process or hangs/times out.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(snExecutionCount, 1);
}

// Regression test: a callback snoozing itself to reschedule must not have
// that snooze silently discarded. After _function() returned, the worker
// thread previously overwrote _executionTime unconditionally with a
// ~10-year deadline, clobbering whatever snooze() had just set.
TEST_F(SnoozableTimerTest, CallbackCanSnoozeItself) {
    CommonUtils::SnoozableTimer* self = nullptr;
    CommonUtils::SnoozableTimer timer([&self]()
    {
        if (++snExecutionCount < 3)
        {
            self->Snooze();
        }
    }, 30);
    self = &timer;

    timer.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(snExecutionCount, 3);
    timer.Stop();
}
