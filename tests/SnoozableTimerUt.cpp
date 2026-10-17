// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "SnoozableTimer.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace
{

using namespace std::chrono_literals;

// Poll an atomic until it reaches `expect` or `timeout` elapses.
// Prefer this over sleep_for + EXPECT: under CI load, sleep_for can return
// much later than requested, so wall-clock ladders with ~10ms slack flake
// even when the timer itself is correct.
bool WaitUntil(const std::atomic<int>& value, int expect, std::chrono::milliseconds timeout)
{
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (value.load() < expect)
   {
      if (std::chrono::steady_clock::now() >= deadline)
      {
         return false;
      }
      std::this_thread::sleep_for(1ms);
   }
   return true;
}

// Brief pause used only as a lower-bound probe ("still shouldn't have
// fired"). Kept well under the snooze period so scheduling jitter is
// unlikely to jump past the deadline. Never used as proof that a fire
// happened -- that always goes through WaitUntil.
void ProbeSleep(std::chrono::milliseconds d)
{
   std::this_thread::sleep_for(d);
}

} // namespace

class SnoozableTimerTest : public ::testing::Test
{
protected:
   void SetUp() override { snExecutionCount = 0; }

   // atomic: written from the timer's worker thread (via _incrementCount,
   // invoked as the SnoozableTimer callback), read from the test's main
   // thread in every EXPECT below. A plain int here is a real data race
   // (caught by ThreadSanitizer) even though it "worked" in practice on
   // x86 -- relying on an intervening sleep_for() is not a substitute for
   // actual synchronization.
   static std::atomic<int> snExecutionCount;
   std::function<void()> _incrementCount = [&]() {
      snExecutionCount++;
   };
};

std::atomic<int> SnoozableTimerTest::snExecutionCount{0};

TEST_F(SnoozableTimerTest, ExecutesAtSpecifiedTime)
{
   CommonUtils::SnoozableTimer executor(_incrementCount, 200);
   executor.Start();

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, AddTimeBeforeExecution)
{
   // UpdateSnoozePeriod also snoozes: deadline becomes now+500ms.
   CommonUtils::SnoozableTimer executor(_incrementCount, 500);
   executor.Start();
   executor.UpdateSnoozePeriod(500);

   ProbeSleep(100ms);
   EXPECT_EQ(snExecutionCount.load(), 0);

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, ExecutesImmediatelyForPastTime)
{
   CommonUtils::SnoozableTimer executor(_incrementCount, -500);
   executor.Start();

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, MultipleStartsAreIdempotent)
{
   CommonUtils::SnoozableTimer executor(_incrementCount, 200);
   executor.Start();
   executor.Start(); // Second start should do nothing

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, StopBeforeExecution)
{
   // Long period so a short probe + Stop cannot race the deadline under load.
   CommonUtils::SnoozableTimer executor(_incrementCount, 2000);
   executor.Start();

   ProbeSleep(50ms);
   executor.Stop();

   // If Stop failed to cancel, the original 2s deadline would still fire.
   ProbeSleep(500ms);
   EXPECT_EQ(snExecutionCount.load(), 0);
}

TEST_F(SnoozableTimerTest, AddNegativeTime)
{
   CommonUtils::SnoozableTimer executor(_incrementCount, 1000);
   executor.Start();

   executor.UpdateSnoozePeriod(-500);
   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, DestructorCleansUp)
{
   {
      CommonUtils::SnoozableTimer executor(_incrementCount, 500);
      executor.Start();
   } // Destructor stops the timer

   // Past the original deadline: must not fire after destruction.
   ProbeSleep(800ms);
   EXPECT_EQ(snExecutionCount.load(), 0);
}

TEST_F(SnoozableTimerTest, ZeroDuration)
{
   // Deadline is "now" before the worker is spawned, so firing is gated by
   // thread-creation latency. WaitUntil absorbs that scheduling spike.
   CommonUtils::SnoozableTimer executor(_incrementCount, 0);
   executor.Start();

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, SingleSnooze)
{
   constexpr int periodMs = 200;
   CommonUtils::SnoozableTimer executor(_incrementCount, periodMs);
   executor.Start();

   ProbeSleep(40ms);
   executor.Snooze();
   EXPECT_EQ(snExecutionCount.load(), 0);

   // Still inside the new snooze window.
   ProbeSleep(40ms);
   EXPECT_EQ(snExecutionCount.load(), 0);

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, MultiSnooze)
{
   constexpr int periodMs = 200;
   CommonUtils::SnoozableTimer executor(_incrementCount, periodMs);
   executor.Start();

   for (int i = 0; i < 10; ++i)
   {
      ProbeSleep(30ms);
      executor.Snooze();
      EXPECT_EQ(snExecutionCount.load(), 0) << "fired during snooze loop at i=" << i;
   }

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 1);
}

TEST_F(SnoozableTimerTest, SnoozeTimeoutSnoozeAgain)
{
   constexpr int periodMs = 200;
   CommonUtils::SnoozableTimer executor(_incrementCount, periodMs);
   executor.Start();

   ProbeSleep(40ms);
   executor.Snooze();
   EXPECT_EQ(snExecutionCount.load(), 0);

   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));

   // After a fire with no self-snooze, the worker parks; must not re-fire
   // on its own.
   ProbeSleep(300ms);
   EXPECT_EQ(snExecutionCount.load(), 1);

   // Snooze after park schedules a second execution.
   executor.Snooze();
   ProbeSleep(40ms);
   EXPECT_EQ(snExecutionCount.load(), 1);

   EXPECT_TRUE(WaitUntil(snExecutionCount, 2, 2s));
   executor.Stop();
   EXPECT_EQ(snExecutionCount.load(), 2);
}

// Regression test: a callback stopping the timer it belongs to must not
// crash. stop() previously called join() on the thread it was invoked
// from -- a self-join, which is undefined behavior (std::terminate() in
// practice, since the resulting exception escapes the thread's entry
// function uncaught).
TEST_F(SnoozableTimerTest, CallbackCanStopItself)
{
   CommonUtils::SnoozableTimer* self = nullptr;
   CommonUtils::SnoozableTimer timer(
       [&self]() {
          snExecutionCount++;
          self->Stop();
       },
       20);
   self = &timer;

   timer.Start();

   // If the callback's stop() call crashes or deadlocks, this test aborts
   // the process or hangs/times out.
   EXPECT_TRUE(WaitUntil(snExecutionCount, 1, 2s));
   EXPECT_EQ(snExecutionCount.load(), 1);
}

// Regression test: a callback snoozing itself to reschedule must not have
// that snooze silently discarded. After _function() returned, the worker
// thread previously overwrote _executionTime unconditionally with a
// ~10-year deadline, clobbering whatever snooze() had just set.
TEST_F(SnoozableTimerTest, CallbackCanSnoozeItself)
{
   CommonUtils::SnoozableTimer* self = nullptr;
   CommonUtils::SnoozableTimer timer(
       [&self]() {
          if (++snExecutionCount < 3)
          {
             self->Snooze();
          }
       },
       30);
   self = &timer;

   timer.Start();

   EXPECT_TRUE(WaitUntil(snExecutionCount, 3, 2s));
   timer.Stop();
   EXPECT_EQ(snExecutionCount.load(), 3);
}
