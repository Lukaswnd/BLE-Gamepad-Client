#include "logger.h"

#include <esp_log.h>
#include <cstdint>

namespace blegc {

std::atomic<int> g_logLevel{CONFIG_BT_BLEGC_LOG_LEVEL > 5 ? 5 : CONFIG_BT_BLEGC_LOG_LEVEL};

void applyLogLevel() {
  esp_log_level_set(BLEGC_LOG_TAG, static_cast<esp_log_level_t>(g_logLevel.load()));
}

void setDefaultLogLevel() {
  g_logLevel.store(CONFIG_BT_BLEGC_LOG_LEVEL > 5 ? 5 : CONFIG_BT_BLEGC_LOG_LEVEL);
  applyLogLevel();
}

void setLogLevelDebug() {
  g_logLevel.store(static_cast<int>(ESP_LOG_DEBUG));
  applyLogLevel();
}

void logBufferHex(const esp_log_level_t level, const char* tag, const uint8_t* buf, std::size_t bufLen) {
  static const char hex[] = "0123456789abcdef";
  constexpr std::size_t BYTES_PER_LINE = 16;

  if (!buf) {
    return;
  }

  char line[BYTES_PER_LINE * 3 + 1];  // "xx xx ... " + '\0'

  while (bufLen > 0) {
    std::size_t lineLen = bufLen < BYTES_PER_LINE ? bufLen : BYTES_PER_LINE;
    char* p = line;

    for (std::size_t i = 0; i < lineLen; ++i) {
      uint8_t v = buf[i];
      *p++ = hex[v >> 4];
      *p++ = hex[v & 0x0f];
      *p++ = ' ';
    }

    *p = '\0';

    ESP_LOG_LEVEL(level, tag, "%s", line);

    buf += lineLen;
    bufLen -= lineLen;
  }
}

void logBufferBin(const esp_log_level_t level, const char* tag, const uint8_t* buf, const std::size_t bufLen) {
  if (!buf) {
    return;
  }

  char line[9];  // 8 bits + '\0'

  for (std::size_t i = 0; i < bufLen; ++i) {
    const uint8_t v = buf[i];
    for (int b = 0; b < 8; ++b) {
      line[b] = (v & (0x80 >> b)) ? '1' : '0';
    }
    line[8] = '\0';

    ESP_LOG_LEVEL(level, tag, "%s", line);
  }
}

}  // namespace blegc
