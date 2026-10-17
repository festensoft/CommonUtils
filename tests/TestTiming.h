// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef COMMONUTILS_TEST_TIMING_H_
#define COMMONUTILS_TEST_TIMING_H_

#include <atomic>
#include <chrono>
#include <thread>

namespace CommonUtils::Test
{

using namespace std::chrono_literals;

// Test-only helpers for timing-sensitive suites (Timer, SnoozableTimer).
// Prefer WaitUntil over sleep_for + EXPECT: under CI load, a fixed sleep can
// return before the Nth tick has run, or after a "must not have fired yet"
// window has already closed.

template <typename Pred>
inline bool WaitUntil(Pred pred, std::chrono::milliseconds timeout)
{
   const auto deadline = std::chrono::steady_clock::now() + timeout;
   while (!pred())
   {
      if (std::chrono::steady_clock::now() >= deadline)
      {
         return false;
      }
      std::this_thread::sleep_for(1ms);
   }
   return true;
}

inline bool WaitUntil(const std::atomic<int>& value, int expect,
                      std::chrono::milliseconds timeout)
{
   return WaitUntil([&] { return value.load() >= expect; }, timeout);
}

// Brief pause used only as a lower-bound probe ("still shouldn't have
// fired"). Keep well under the timer period. Never use as proof that a
// fire happened -- that always goes through WaitUntil.
inline void ProbeSleep(std::chrono::milliseconds d)
{
   std::this_thread::sleep_for(d);
}

} // namespace CommonUtils::Test

#endif // COMMONUTILS_TEST_TIMING_H_
