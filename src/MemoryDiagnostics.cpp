#include "MemoryDiagnostics.h"

#include <esp_heap_caps.h>

#include "AppLog.h"

namespace MemoryDiagnostics {

void log(const char *label) {
  AppLog.print("Memory ");
  AppLog.print(label == nullptr ? "snapshot" : label);
  AppLog.print(": free_heap=");
  AppLog.print(ESP.getFreeHeap());
  AppLog.print(" min_free_heap=");
  AppLog.print(ESP.getMinFreeHeap());
  AppLog.print(" internal_free=");
  AppLog.print(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  AppLog.print(" internal_largest=");
  AppLog.print(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (psramFound()) {
    AppLog.print(" psram_free=");
    AppLog.print(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    AppLog.print(" psram_largest=");
    AppLog.print(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  AppLog.println();
}

}  // namespace MemoryDiagnostics
