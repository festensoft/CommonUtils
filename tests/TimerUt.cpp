// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "Timer.h"
#include "TestTiming.h"

#include <gtest/gtest.h>

#include <atomic>
#include <functional>

using CommonUtils::Test::ProbeSleep;
using CommonUtils::Test::WaitUntil;
using namespace std::chrono_literals;

TEST(TimerTest, OneTimeExecution)
{
   CommonUtils::Timer timer;
   std::atomic<bool> flag{false};

   timer.StartOneShot([&flag]() { flag = true; }, 100);

   EXPECT_TRUE(WaitUntil([&] { return flag.load(); }, 2s));
   timer.Stop();
   EXPECT_TRUE(flag.load());
}

TEST(TimerTest, PeriodicExecution)
{
   CommonUtils::Timer timer;
   std::atomic<int> counter{0};

   timer.StartPeriodic([&counter]() { ++counter; }, 50);

   // Wait for the effect (3 ticks), not a wall-clock multiple of the period.
   EXPECT_TRUE(WaitUntil(counter, 3, 2s));
   timer.Stop();
   EXPECT_GE(counter.load(), 3);
}

TEST(TimerTest, Cancel)
{
   CommonUtils::Timer timer;
   std::atomic<bool> flag{false};

   // Long delay so Stop after a short probe cannot race the deadline.
   timer.StartOneShot([&flag]() { flag = true; }, 2000);

   ProbeSleep(50ms);
   timer.Stop();

   // Past enough time that an uncanceled 2s one-shot would have fired.
   ProbeSleep(500ms);
   EXPECT_FALSE(flag.load());
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
   ASSERT_TRUE(WaitUntil(fireCount, 1, 2s));

   // Must not crash (std::terminate), even though stop() was never called.
   timer.StartOneShot([&fireCount]() { ++fireCount; }, 50);
   EXPECT_TRUE(WaitUntil(fireCount, 2, 2s));
}

// Regression test: a periodic callback stopping the timer it belongs to
// must not deadlock. func() previously ran while holding the Timer's
// internal mutex, so a callback calling stop() would try to re-lock a
// non-recursive std::mutex already held by the same thread.
TEST(TimerTest, CallbackCanStopItself)
{
   CommonUtils::Timer timer;
   std::atomic<int> counter{0};

   timer.StartPeriodic(
       [&timer, &counter]() {
          if (++counter >= 3)
          {
             timer.Stop();
          }
       },
       50);

   // If the callback's stop() call deadlocks, this test hangs/times out.
   EXPECT_TRUE(WaitUntil(counter, 3, 2s));
   EXPECT_EQ(counter.load(), 3);

   // Confirm it actually stopped rather than continuing to fire.
   ProbeSleep(300ms);
   EXPECT_EQ(counter.load(), 3);
}

// Regression test: a one-shot callback re-arming itself must not crash or
// corrupt state, even though it starts a new thread while the old one
// (running the callback) is still finishing up.
TEST(TimerTest, CallbackCanRestartItself)
{
   CommonUtils::Timer timer;
   std::atomic<int> counter{0};
   std::function<void()> reArm;
   reArm = [&timer, &counter, &reArm]() {
      if (++counter < 3)
      {
         timer.StartOneShot(reArm, 50);
      }
   };

   timer.StartOneShot(reArm, 50);

   EXPECT_TRUE(WaitUntil(counter, 3, 2s));
   timer.Stop();
   EXPECT_EQ(counter.load(), 3);
}
