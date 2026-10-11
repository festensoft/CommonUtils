// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include <gtest/gtest.h>
#include "Timer.h"
#include <atomic>
#include <functional>
#include <thread>

// Test for one-time execution
TEST(TimerTest, OneTimeExecution) 
{
    CommonUtils::Timer timer;
    std::atomic<bool> flag{false};

    timer.StartOneShot([&flag]()
    {
        flag = true;
    }, 100); // Execute after 100 milliseconds

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(flag);
}

// Test for periodic execution
TEST(TimerTest, PeriodicExecution) 
{
    CommonUtils::Timer timer;
    std::atomic<int> counter{0};

    timer.StartPeriodic([&counter]() 
    {
        ++counter;
    }, 100); // Execute every 100 milliseconds

    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    timer.Stop();
    EXPECT_GE(counter, 3); // At least 3 executions expected
}

// Test for cancellation
TEST(TimerTest, Cancel)
{
    CommonUtils::Timer timer;
    std::atomic<bool> flag{false};

    timer.StartOneShot([&flag]()
    {
        flag = true;
    }, 500); // Execute after 500 milliseconds

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    timer.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_FALSE(flag);
}

// Regression test: restarting a Timer after it already fired naturally
// (without an intervening stop()) must not crash. The thread from a
// completed one-shot was previously never joined/detached, so
// std::thread's assignment operator would call std::terminate() the next
// time the timer was started.
TEST(TimerTest, RestartAfterNaturalCompletion)
{
    CommonUtils::Timer timer;
    std::atomic<int> fireCount{0};

    timer.StartOneShot([&fireCount]() { ++fireCount; }, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_EQ(fireCount, 1);

    // Must not crash (std::terminate), even though stop() was never called.
    timer.StartOneShot([&fireCount]() { ++fireCount; }, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(fireCount, 2);
}

// Regression test: a periodic callback stopping the timer it belongs to
// must not deadlock. func() previously ran while holding the Timer's
// internal mutex, so a callback calling stop() would try to re-lock a
// non-recursive std::mutex already held by the same thread.
TEST(TimerTest, CallbackCanStopItself)
{
    CommonUtils::Timer timer;
    std::atomic<int> counter{0};

    timer.StartPeriodic([&timer, &counter]()
    {
        if (++counter >= 3)
        {
            timer.Stop();
        }
    }, 50);

    // If the callback's stop() call deadlocks, this test hangs/times out.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(counter, 3);

    // Confirm it actually stopped rather than continuing to fire.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(counter, 3);
}

// Regression test: a one-shot callback re-arming itself must not crash or
// corrupt state, even though it starts a new thread while the old one
// (running the callback) is still finishing up.
TEST(TimerTest, CallbackCanRestartItself)
{
    CommonUtils::Timer timer;
    std::atomic<int> counter{0};
    std::function<void()> reArm;
    reArm = [&timer, &counter, &reArm]()
    {
        if (++counter < 3)
        {
            timer.StartOneShot(reArm, 50);
        }
    };

    timer.StartOneShot(reArm, 50);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(counter, 3);
}
