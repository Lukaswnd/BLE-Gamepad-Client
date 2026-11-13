#include "BLEValueWriter.h"

#include <NimBLEDevice.h>
#include <algorithm>
#include <cstring>
#include <new>
#include "config.h"
#include "logger.h"
#include "utils.h"
#include "xbox/XboxVibrationsCommand.h"

constexpr size_t MAX_CAPACITY = CONFIG_BT_BLEGC_WRITER_BUFFER_MAX_CAPACITY;

template <typename T>
BLEValueWriter<T>::BLEValueWriter()
    : _pChar(nullptr), _mutexBuffer(), _mutex(nullptr), _inlineBuffer(), _pBuffer(_inlineBuffer),
      _capacity(sizeof(_inlineBuffer)) {
  _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer);  // no heap, safe in global constructors
}

template <typename T>
BLEValueWriter<T>::~BLEValueWriter() {
  if (_pBuffer != _inlineBuffer) {
    delete[] _pBuffer;
  }
  vSemaphoreDelete(_mutex);
}

template <typename T>
bool BLEValueWriter<T>::init(NimBLERemoteCharacteristic* pChar) {
  if (!pChar) {
    return false;
  }

  if (!pChar->canWrite() && !pChar->canWriteNoResponse()) {
    BLEGC_LOGE("Characteristic not able to write. %s", blegc::remoteCharToStr(pChar).c_str());
    return false;
  }

  xSemaphoreTake(_mutex, portMAX_DELAY);
  _pChar = pChar;
  xSemaphoreGive(_mutex);
  return true;
}

template <typename T>
void BLEValueWriter<T>::reset() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  _pChar = nullptr;
  xSemaphoreGive(_mutex);
}

template <typename T>
void BLEValueWriter<T>::write(T& cmd) {
  xSemaphoreTake(_mutex, portMAX_DELAY);

  if (!_pChar) {
    xSemaphoreGive(_mutex);
    BLEGC_LOGD("Controller not connected, write skipped");
    return;
  }

  size_t used = 0;
  BLEEncodeResult result;
  while ((result = cmd.encode(used, _pBuffer, _capacity)) == BLEEncodeResult::BufferTooShort &&
         _capacity < MAX_CAPACITY) {
    const size_t newCapacity = std::min(_capacity * 2, MAX_CAPACITY);
    auto* pNew = new (std::nothrow) uint8_t[newCapacity];
    if (!pNew) {
      break;
    }
    if (_pBuffer != _inlineBuffer) {
      delete[] _pBuffer;
    }
    _pBuffer = pNew;
    _capacity = newCapacity;
  }

  bool sent = false;
  if (result == BLEEncodeResult::Success && used > 0 && used <= _capacity) {
#if CONFIG_BT_BLEGC_LOG_BUFFER_ENABLED
    cmd.reportData = std::shared_ptr<uint8_t[]>(new uint8_t[used]);
    cmd.reportDataLen = used;
    cmd.reportDataCap = used;
    memcpy(cmd.reportData.get(), _pBuffer, used);
#endif
    // write without response: queued by NimBLE, does not wait for the peer
    sent = _pChar->writeValue(_pBuffer, used, false);
  }

  xSemaphoreGive(_mutex);

  switch (result) {
    case BLEEncodeResult::Success:
      if (!sent) {
        BLEGC_LOGW("Write failed");
      }
      break;
    case BLEEncodeResult::InvalidValue:
      BLEGC_LOGE("Encoding failed, invalid value");
      break;
    case BLEEncodeResult::BufferTooShort:
      BLEGC_LOGE("Encoding failed, buffer too short");
      break;
  }
}

template class BLEValueWriter<XboxVibrationsCommand>;
