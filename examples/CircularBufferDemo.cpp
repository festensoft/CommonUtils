// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#include "CircularBuffer.h"
#include "Demos.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

void RunCircularBufferDemo()
{
   std::cout << "=== CircularBuffer demo ===\n";

   CommonUtils::CircularBuffer<int> buffer(5);
   for (int i = 1; i <= 8; ++i)
   {
      buffer.Push(i);
   }
   std::cout << "Pushed 1..8 into a capacity-5 buffer (oldest 3 were evicted).\n";
   std::cout << "size=" << buffer.Size() << " capacity=" << buffer.Capacity()
             << " full=" << std::boolalpha << buffer.Full() << '\n';

   std::cout << "Contents (oldest -> newest): ";
   for (const auto& v : buffer.ToVector())
   {
      std::cout << v << ' ';
   }
   std::cout << "\nback()=" << buffer.Back() << '\n';

   // Thread safety: one producer thread pushing while the main thread
   // reads concurrently -- every public method locks internally, so this
   // is safe.
   std::cout << "Concurrent push/read demo (1 producer, 500 pushes)...\n";
   CommonUtils::CircularBuffer<int> shared(50);
   std::thread producer([&shared]() {
      for (int i = 0; i < 500; ++i)
      {
         shared.Push(i);
      }
   });

   for (int i = 0; i < 5; ++i)
   {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      std::cout << "  size while producer runs: " << shared.Size() << '\n';
   }
   producer.join();
   std::cout << "Final size: " << shared.Size() << " (capacity " << shared.Capacity() << ")\n\n";
}
