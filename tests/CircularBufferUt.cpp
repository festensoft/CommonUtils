// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "CircularBuffer.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

using CommonUtils::CircularBuffer;

TEST(CircularBufferTest, ThrowsOnZeroCapacity)
{
   EXPECT_THROW(const CircularBuffer<int> buffer(0), std::invalid_argument);
}

TEST(CircularBufferTest, StartsEmpty)
{
   const CircularBuffer<int> buffer(3);
   EXPECT_TRUE(buffer.Empty());
   EXPECT_FALSE(buffer.Full());
   EXPECT_EQ(buffer.Size(), 0u);
   EXPECT_EQ(buffer.Capacity(), 3u);
}

TEST(CircularBufferTest, PushBelowCapacityPreservesOrder)
{
   CircularBuffer<int> buffer(5);
   buffer.Push(1);
   buffer.Push(2);
   buffer.Push(3);

   EXPECT_EQ(buffer.Size(), 3u);
   EXPECT_FALSE(buffer.Full());
   EXPECT_EQ(buffer[0], 1);
   EXPECT_EQ(buffer[1], 2);
   EXPECT_EQ(buffer[2], 3);
   EXPECT_EQ(buffer.Back(), 3);
}

TEST(CircularBufferTest, PushPastCapacityOverwritesOldest)
{
   CircularBuffer<int> buffer(3);
   buffer.Push(1);
   buffer.Push(2);
   buffer.Push(3);
   buffer.Push(4); // overwrites 1
   buffer.Push(5); // overwrites 2

   EXPECT_TRUE(buffer.Full());
   EXPECT_EQ(buffer.Size(), 3u);
   EXPECT_EQ(buffer[0], 3);
   EXPECT_EQ(buffer[1], 4);
   EXPECT_EQ(buffer[2], 5);
   EXPECT_EQ(buffer.Back(), 5);
}

TEST(CircularBufferTest, AtThrowsOutOfRange)
{
   CircularBuffer<int> buffer(3);
   buffer.Push(1);
   EXPECT_EQ(buffer.At(0), 1);
   EXPECT_THROW((void)buffer.At(1), std::out_of_range);
}

TEST(CircularBufferTest, BackThrowsWhenEmpty)
{
   const CircularBuffer<int> buffer(3);
   EXPECT_THROW((void)buffer.Back(), std::out_of_range);
}

TEST(CircularBufferTest, PushRangeAndVector)
{
   CircularBuffer<int> buffer(4);
   const int raw[] = {1, 2, 3};
   buffer.Push(raw, 3);
   buffer.Push(std::vector<int>{4, 5}); // 5 pushes total, capacity 4: drops 1

   EXPECT_EQ(buffer.ToVector(), (std::vector<int>{2, 3, 4, 5}));
}

TEST(CircularBufferTest, ClearResetsSizeAndHead)
{
   CircularBuffer<int> buffer(3);
   buffer.Push(1);
   buffer.Push(2);
   buffer.Clear();

   EXPECT_TRUE(buffer.Empty());
   EXPECT_EQ(buffer.Size(), 0u);

   buffer.Push(9);
   EXPECT_EQ(buffer[0], 9);
}

TEST(CircularBufferTest, ToVectorChronologicalOrder)
{
   CircularBuffer<int> buffer(3);
   for (int i = 1; i <= 5; ++i)
   {
      buffer.Push(i);
   }
   EXPECT_EQ(buffer.ToVector(), (std::vector<int>{3, 4, 5}));
}

// Thread safety: many producer threads pushing concurrently, plus a reader
// thread continuously calling the locked accessors, must never crash or
// report a size exceeding capacity.
//
// Note: this deliberately does NOT compare results across separate locked
// calls (e.g. size() against a separately-called full()) -- the class
// documents no cross-call atomicity, so two independent calls can
// legitimately observe different points in time relative to a concurrent
// push() and appear "inconsistent" without indicating any real bug. Each
// check below only relies on a single call's own result.
TEST(CircularBufferTest, ConcurrentPushAndReadDoNotCrashOrCorrupt)
{
   constexpr std::size_t CAPACITY = 100;
   constexpr int NUM_PRODUCERS = 8;
   constexpr int PUSHES_PER_PRODUCER = 2000;

   CircularBuffer<int> buffer(CAPACITY);
   std::atomic<bool> stopReading{false};
   std::atomic<bool> invariantViolated{false};

   std::thread reader([&]() {
      while (!stopReading)
      {
         if (buffer.Size() > CAPACITY)
         {
            invariantViolated = true;
         }
         if (buffer.ToVector().size() > CAPACITY)
         {
            invariantViolated = true;
         }
      }
   });

   std::vector<std::thread> producers;
   producers.reserve(NUM_PRODUCERS);
   for (int p = 0; p < NUM_PRODUCERS; ++p)
   {
      producers.emplace_back([&, p]() {
         for (int i = 0; i < PUSHES_PER_PRODUCER; ++i)
         {
            buffer.Push((p * PUSHES_PER_PRODUCER) + i);
         }
      });
   }

   for (auto& t : producers)
   {
      t.join();
   }
   stopReading = true;
   reader.join();

   EXPECT_FALSE(invariantViolated);
   EXPECT_EQ(buffer.Size(), CAPACITY);
   EXPECT_TRUE(buffer.Full());
}

// Thread safety: push(vector) applies its whole batch under a single lock
// acquisition, so a concurrent reader must never observe a partially
// applied batch.
TEST(CircularBufferTest, BatchPushIsAtomicUnderConcurrentReads)
{
   constexpr std::size_t CAPACITY = 10;
   constexpr int ITERATIONS = 2000;

   CircularBuffer<int> buffer(CAPACITY);
   std::vector<int> batch(CAPACITY);
   std::iota(batch.begin(), batch.end(), 1); // 1..10, all distinct

   std::atomic<bool> stop{false};
   std::atomic<bool> torn{false};

   std::thread reader([&]() {
      while (!stop)
      {
         const std::vector<int> snapshot = buffer.ToVector();
         // Each push(batch) call fully replaces the buffer's contents
         // (capacity == batch size), so a fully-applied batch is always
         // exactly {1..10} in order. Anything else observed at full
         // size means the reader caught a torn, partially-applied
         // batch.
         if (snapshot.size() == CAPACITY && snapshot != batch)
         {
            torn = true;
         }
      }
   });

   std::thread writer([&]() {
      for (int i = 0; i < ITERATIONS; ++i)
      {
         buffer.Push(batch);
      }
   });

   writer.join();
   stop = true;
   reader.join();

   EXPECT_FALSE(torn);
}

// Thread safety: with shared_ptr elements, a reader must never observe a
// null or otherwise inconsistent element while a writer is concurrently
// pushing -- a proxy for the mutex genuinely serializing element-level
// access rather than just the index bookkeeping.
TEST(CircularBufferTest, ConcurrentAccessWithSharedPtrElements)
{
   constexpr std::size_t CAPACITY = 4;
   CircularBuffer<std::shared_ptr<int>> buffer(CAPACITY);

   std::atomic<bool> stop{false};
   std::atomic<bool> failed{false};

   std::thread reader([&]() {
      while (!stop)
      {
         const std::size_t size = buffer.Size();
         for (std::size_t i = 0; i < size; ++i)
         {
            const std::shared_ptr<int> elem = buffer.At(i);
            if (elem && *elem < 0)
            {
               failed = true;
            }
         }
      }
   });

   std::thread writer([&]() {
      for (int i = 0; i < 5000; ++i)
      {
         buffer.Push(std::make_shared<int>(i));
      }
   });

   writer.join();
   stop = true;
   reader.join();

   EXPECT_FALSE(failed);
}
