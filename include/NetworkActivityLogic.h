#pragma once

#include <stdint.h>

namespace NetworkActivityLogic {

inline bool isSlow(uint32_t elapsedMs, uint32_t slowWarningMs) {
  return elapsedMs >= slowWarningMs;
}

template <typename HttpLike>
void configureHttp(HttpLike &http, uint32_t connectTimeoutMs, uint32_t ioTimeoutMs) {
  http.setConnectTimeout(connectTimeoutMs);
  http.setTimeout(ioTimeoutMs);
  http.setReuse(false);
}

}  // namespace NetworkActivityLogic
