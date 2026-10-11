// SPDX-License-Identifier: BSL-1.0
// Copyright 2026 Steven Festenstein

#ifndef COMMONUTILS_CIRCULARBUFFER_H_
#define COMMONUTILS_CIRCULARBUFFER_H_

// System headers
#include <algorithm>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace CommonUtils
{

/**
 * @class CircularBuffer
 * @brief Thread-safe, fixed-capacity circular buffer for time-series history.
 * Stores the most recent `capacity` elements; older entries are silently
 * overwritten.
 *
 * Thread safety: every public method locks an internal mutex, so calls
 * from multiple threads are individually safe (no data race, no torn
 * reads). Element-returning accessors (operator[], at(), back()) return by
 * value -- a copy taken while the lock is held -- rather than by
 * reference, since a reference into the internal storage would not be
 * safe to use once the lock is released (a concurrent push() could
 * overwrite that slot at any time). A single push(range)/push(vector)
 * call applies the whole batch under one lock acquisition, so it is
 * atomic as a whole with respect to concurrent readers.
 *
 * There is no cross-call atomicity beyond that: e.g. a size() followed by
 * a separate operator[](size() - 1) call can race with a concurrent
 * push() in between the two calls. Use toVector() for a consistent
 * point-in-time snapshot of the whole buffer instead.
 *
 * T must be default-constructible (every slot is value-initialized up
 * front) and copyable (elements are stored and returned by value).
 */
template <typename T>
class CircularBuffer
{
public:
   /**
    * @brief Construct a buffer with the given maximum capacity.
    * @throws std::invalid_argument if capacity is zero.
    */
   explicit CircularBuffer(std::size_t capacity)
      : _data(ValidateCapacity(capacity))
      , _capacity{capacity}
   {
   }

   /** @brief Push a single element, overwriting the oldest if full. */
   void Push(const T& value)
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      PushLocked(value);
   }

   /** @brief Push a range of elements as a single atomic batch. */
   void Push(const T* data, std::size_t count)
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      for (std::size_t i = 0; i < count; ++i)
      {
         PushLocked(data[i]);
      }
   }

   /** @brief Push a vector of elements as a single atomic batch. */
   void Push(const std::vector<T>& values)
   {
      Push(values.data(), values.size());
   }

   /** @brief Access element by logical index (0 = oldest), by value. */
   [[nodiscard]] T operator[](std::size_t index) const
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      return _data[PhysicalIndex(index)];
   }

   /** @brief Access element by logical index with bounds checking, by value. */
   [[nodiscard]] T At(std::size_t index) const
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      if (index >= _size)
      {
         throw std::out_of_range("CircularBuffer::at index out of range");
      }
      return _data[PhysicalIndex(index)];
   }

   /** @brief Return the most recently pushed element, by value. */
   [[nodiscard]] T Back() const
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      if (_size == 0)
      {
         throw std::out_of_range("CircularBuffer::back called on empty buffer");
      }
      return _data[(_head + _capacity - 1) % _capacity];
   }

   /** @brief Copy all stored elements in chronological order into a vector. */
   [[nodiscard]] std::vector<T> ToVector() const
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      std::vector<T> result;
      result.reserve(_size);
      for (std::size_t i = 0; i < _size; ++i)
      {
         result.push_back(_data[PhysicalIndex(i)]);
      }
      return result;
   }

   /** @brief Number of elements currently stored. */
   [[nodiscard]] std::size_t Size() const
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      return _size;
   }

   /**
    * @brief Maximum number of elements the buffer can hold.
    *
    * Not locked: _capacity is fixed at construction and never changes
    * afterward, so concurrent reads are inherently race-free.
    */
   [[nodiscard]] std::size_t Capacity() const { return _capacity; }

   /** @brief True if the buffer has been filled at least once. */
   [[nodiscard]] bool Full() const
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      return _size == _capacity;
   }

   /** @brief True if no elements have been stored. */
   [[nodiscard]] bool Empty() const
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      return _size == 0;
   }

   /** @brief Remove all elements without changing capacity. */
   void Clear()
   {
      const std::lock_guard<std::mutex> lock(_mutex);
      _head = 0;
      _size = 0;
   }

private:
   [[nodiscard]] static std::size_t ValidateCapacity(std::size_t capacity)
   {
      if (capacity == 0)
      {
         throw std::invalid_argument("CircularBuffer capacity must be > 0");
      }
      return capacity;
   }

   /** @brief Push a single element. Assumes _mutex is already held. */
   void PushLocked(const T& value)
   {
      _data[_head] = value;
      _head = (_head + 1) % _capacity;
      if (_size < _capacity)
      {
         ++_size;
      }
   }

   /** @brief Map a logical index to a physical slot. Assumes _mutex is already held. */
   [[nodiscard]] std::size_t PhysicalIndex(std::size_t logicalIndex) const
   {
      if (_size < _capacity)
      {
         return logicalIndex;
      }
      return (_head + logicalIndex) % _capacity;
   }

   std::vector<T> _data;
   std::size_t _capacity;
   std::size_t _head{0};
   std::size_t _size{0};
   mutable std::mutex _mutex;
};

} // namespace CommonUtils

#endif // COMMONUTILS_CIRCULARBUFFER_H_
