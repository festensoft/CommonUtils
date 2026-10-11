#include "DataHandler.h"
#include "Demos.h"
#include "LoggerMacros.h"

#include <chrono>
#include <string>
#include <thread>

void RunDataHandlerDemo()
{
   GPINFO("=== DataHandler demo ===");

   CommonUtils::DataHandler<std::string> handler;
   const int id = handler.RegisterListener(
       [](const std::string& msg) { GPINFO("  listener received: {}", msg); });

   handler.SignalData("hello");
   handler.SignalData("world");

   // Listener callbacks run asynchronously on DataHandler's own worker
   // thread, so give it a moment to catch up before reading watermarkInfo().
   std::this_thread::sleep_for(std::chrono::milliseconds(50));

   const auto [listenerCount, queuedCount] = handler.WatermarkInfo();
   GPINFO("listeners={} queued={}", listenerCount, queuedCount);

   handler.UnregisterListener(id);
}
