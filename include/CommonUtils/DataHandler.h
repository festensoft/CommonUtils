#ifndef COMMONUTILS_DATAHANDLER_H_
#define COMMONUTILS_DATAHANDLER_H_

// System headers
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <map>

#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace CommonUtils
{

/**
 * @class DataHandler
 * @brief Thread-safe queue that dispatches data to registered listeners.
 *
 * Listeners register an std::function to be called when new data is
 * signalled.  All listener callbacks are invoked on a dedicated worker
 * thread, decoupling the producer from the consumers.
 *
 * Listener callbacks run without holding DataHandler's internal locks, so
 * it is safe for a listener to call registerListener()/unregisterListener()
 * (including unregistering itself) or watermarkInfo() on this same
 * DataHandler.
 *
 * Not safe to destroy a DataHandler concurrently with another thread still
 * calling signalData(): the destructor stops the worker thread and drains
 * the queue without data races, but a signalData() call racing the very
 * start of destruction may be silently dropped rather than delivered --
 * ensure producers have stopped before letting a DataHandler go out of
 * scope.
 */
template <typename T>
class DataHandler
{
public:
   using Listener = std::function<void(const T&)>;

   /**
    * @brief Construct a DataHandler and start the worker thread.
    */
   DataHandler() : _stopFlag(false)
   {
      _workerThread = std::thread(&DataHandler::ProcessData, this);
   }

   /**
    * @brief Destroy the DataHandler, stopping the worker thread.
    */
   ~DataHandler()
   {
      // _stopFlag must be set while holding _cvMutex, the same mutex
      // processData()'s wait() checks it under -- otherwise this is the
      // classic condition_variable lost-wakeup race: _stopFlag being
      // atomic only prevents a data race on the flag itself, it does not
      // guarantee the worker thread observes the change in time to catch
      // this notify_all(), which could then hang the join() below forever.
      {
         const std::lock_guard<std::mutex> lock(_cvMutex);
         _stopFlag = true;
      }
      _condVar.notify_all();
      if (_workerThread.joinable())
      {
         _workerThread.join();
      }

      {
         const std::lock_guard<std::mutex> lock(_listenersMutex);
         _listeners.clear();
      }

      // _dataQueue is otherwise always accessed under _cvMutex (see
      // signalData()/processData()); take the same lock here so this
      // can't race a signalData() call that read _stopFlag as false just
      // before it was set above.
      {
         const std::lock_guard<std::mutex> lock(_cvMutex);
         while (!_dataQueue.empty())
         {
            _dataQueue.pop();
         }
      }
   }

   // Non-copyable.
   DataHandler(const DataHandler&) = delete;
   DataHandler& operator=(const DataHandler&) = delete;

   /**
    * @brief Push new data to the queue and notify the worker thread.
    *
    * @param data The data item to enqueue.
    */
   void SignalData(const T& data)
   {
      if (_stopFlag) return;

      const std::lock_guard<std::mutex> lock(_cvMutex);
      {
         _dataQueue.push(data);
      }
      _condVar.notify_one();
   }

   /**
    * @brief Register a listener callback for new data.
    *
    * @param listener Callback invoked (on the worker thread) for each item.
    * @return A unique registration ID, or -1 if the handler is stopped.
    */
   int RegisterListener(const Listener& listener)
   {
      if (_stopFlag) return -1;
      const std::lock_guard<std::mutex> lock(_listenersMutex);
      _nextListenerId++;
      _listeners[_nextListenerId] = listener;
      return _nextListenerId;
   }

   /**
    * @brief Unregister a listener by its registration ID.
    *
    * @param id The registration ID returned by registerListener().
    */
   void UnregisterListener(int id)
   {
      if (_stopFlag) return;
      const std::lock_guard<std::mutex> lock(_listenersMutex);
      _listeners.erase(id);
   }

   /**
    * @brief Get usage statistics.
    *
    * @return Pair of (listener count, queued item count).
    */
   std::pair<size_t, size_t> WatermarkInfo()
   {
      if (_stopFlag) return {0, 0};

      // _listeners and _dataQueue are guarded by two different mutexes
      // (_listenersMutex and _cvMutex respectively) -- lock both together
      // (deadlock-free regardless of acquisition order) rather than
      // reading _dataQueue under the wrong lock.
      const std::scoped_lock lock(_listenersMutex, _cvMutex);
      return {_listeners.size(), _dataQueue.size()};
   }

private:
   void ProcessData()
   {
      while (!_stopFlag)
      {
         T data;
         {
            std::unique_lock<std::mutex> lock(_cvMutex);
            _condVar.wait(lock, [this] { return !_dataQueue.empty() || _stopFlag; });
            if (_stopFlag && _dataQueue.empty())
            {
               return;
            }
            data = _dataQueue.front();
            _dataQueue.pop();
         }
         NotifyListeners(data);
      }
   }

   void NotifyListeners(const T& data)
   {
      // Snapshot the listeners under the lock, then invoke the copies with
      // the lock released. Never hold _listenersMutex while calling into
      // user code -- a listener that calls registerListener()/
      // unregisterListener() (including unregistering itself, a natural
      // "run once" pattern) or watermarkInfo() on this same DataHandler
      // would otherwise re-lock a non-recursive mutex already held by this
      // (the worker) thread and deadlock.
      std::vector<Listener> listenersSnapshot;
      {
         const std::lock_guard<std::mutex> lock(_listenersMutex);
         listenersSnapshot.reserve(_listeners.size());
         for (const auto& [id, listener] : _listeners)
         {
            listenersSnapshot.push_back(listener);
         }
      }

      for (const auto& listener : listenersSnapshot)
      {
         try
         {
            listener(data);
         }
         catch (const std::exception& e)
         {
            std::cerr << "Listener threw an std::exception! " << e.what() << '\n';
         }
         catch (...)
         {
            std::cerr << "Listener threw an unknown exception!\n";
         }
      }
   }

   std::mutex _listenersMutex;
   std::map<int, Listener> _listeners;
   int _nextListenerId = 123;
   std::queue<T> _dataQueue;
   std::mutex _cvMutex;
   std::condition_variable _condVar;
   std::thread _workerThread;
   std::atomic<bool> _stopFlag;
};

} // namespace CommonUtils

#endif // COMMONUTILS_DATAHANDLER_H_
