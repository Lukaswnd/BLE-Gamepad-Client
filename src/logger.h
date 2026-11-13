#pragma once
#include <esp_log.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef CONFIG_BT_BLEGC_LOG_LEVEL
#if defined(CORE_DEBUG_LEVEL)
#define CONFIG_BT_BLEGC_LOG_LEVEL CORE_DEBUG_LEVEL
#else
#define CONFIG_BT_BLEGC_LOG_LEVEL 1
#endif
#endif

#ifndef CONFIG_BT_BLEGC_LOG_BUFFER_ENABLED
#define CONFIG_BT_BLEGC_LOG_BUFFER_ENABLED 0
#endif

#define BLEGC_LOG_TAG "blegc"

namespace blegc {

extern std::atomic<int> g_logLevel;

/// Cheap runtime check used by the log macros, so log arguments are only evaluated when needed.
inline bool logEnabled(esp_log_level_t level) {
  return static_cast<int>(level) <= g_logLevel.load(std::memory_order_relaxed);
}

void applyLogLevel();

void setDefaultLogLevel();

void setLogLevelDebug();

void logBufferHex(esp_log_level_t level, const char* tag, const uint8_t* buf, std::size_t bufLen);

void logBufferBin(esp_log_level_t level, const char* tag, const uint8_t* buf, std::size_t bufLen);

}  // namespace blegc

#define BLEGC_LOG_AT(level, format, ...)                                \
  do {                                                                  \
    if (blegc::logEnabled(level)) {                                     \
      ESP_LOG_LEVEL(level, BLEGC_LOG_TAG, format, ##__VA_ARGS__);       \
    }                                                                   \
  } while (0)

#define BLEGC_LOGV(format, ...) BLEGC_LOG_AT(ESP_LOG_VERBOSE, format, ##__VA_ARGS__)
#define BLEGC_LOGD(format, ...) BLEGC_LOG_AT(ESP_LOG_DEBUG, format, ##__VA_ARGS__)
#define BLEGC_LOGI(format, ...) BLEGC_LOG_AT(ESP_LOG_INFO, format, ##__VA_ARGS__)
#define BLEGC_LOGW(format, ...) BLEGC_LOG_AT(ESP_LOG_WARN, format, ##__VA_ARGS__)
#define BLEGC_LOGE(format, ...) BLEGC_LOG_AT(ESP_LOG_ERROR, format, ##__VA_ARGS__)

#if CONFIG_BT_BLEGC_LOG_BUFFER_ENABLED
#define BLEGC_LOGD_BUFFER_HEX(buf, bufLen)                                         \
  do {                                                                             \
    if (blegc::logEnabled(ESP_LOG_DEBUG)) {                                        \
      blegc::logBufferHex(ESP_LOG_DEBUG, BLEGC_LOG_TAG, buf, bufLen);              \
    }                                                                              \
  } while (0)
#define BLEGC_LOGD_BUFFER_BIN(buf, bufLen)                                         \
  do {                                                                             \
    if (blegc::logEnabled(ESP_LOG_DEBUG)) {                                        \
      blegc::logBufferBin(ESP_LOG_DEBUG, BLEGC_LOG_TAG, buf, bufLen);              \
    }                                                                              \
  } while (0)
#else
#define BLEGC_LOGD_BUFFER_HEX(buf, bufLen) (void)0
#define BLEGC_LOGD_BUFFER_BIN(buf, bufLen) (void)0
#endif
