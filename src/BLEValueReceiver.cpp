#include "BLEValueReceiver.h"

#include <NimBLEDevice.h>
#include <cstring>
#include <utility>
#include "BLEInternal.h"
#include "logger.h"
#include "utils.h"
#include "steam/SteamControlsState.h"
#include "xbox/XboxBatteryState.h"
#include "xbox/XboxControlsState.h"

template <typename T>
BLEValueReceiver<T>::BLEValueReceiver() = default;

template <typename T>
bool BLEValueReceiver<T>::init(NimBLERemoteCharacteristic* pChar) {
  if (!pChar) {
    return false;
  }

  if (!pChar->canNotify()) {
    BLEGC_LOGE("Characteristic not able to notify. %s", blegc::remoteCharToStr(pChar).c_str());
    return false;
  }

  reset();

  auto handler = [this](NimBLERemoteCharacteristic* c, uint8_t* d, size_t l, bool n) { _handleNotify(c, d, l, n); };

  if (!pChar->subscribe(true, handler, false)) {
    BLEGC_LOGE("Failed to subscribe to notifications. %s", blegc::remoteCharToStr(pChar).c_str());
    return false;
  }

  BLEGC_LOGD("Subscribed to notifications. %s", blegc::remoteCharToStr(pChar).c_str());

  NimBLEAddress address;
  if (auto* pClient = pChar->getClient()) {
    address = pClient->getPeerAddress();
  }

  taskENTER_CRITICAL(&_mux);
  _value.controllerAddress = address;
  taskEXIT_CRITICAL(&_mux);
  return true;
}

template <typename T>
void BLEValueReceiver<T>::reset() {
  T fresh{};
  taskENTER_CRITICAL(&_mux);
  std::swap(_value, fresh);
  taskEXIT_CRITICAL(&_mux);
  // old value (and any report buffer) is released here, outside the critical section
}

template <typename T>
void BLEValueReceiver<T>::read(T* value) {
  if (!value) {
    return;
  }

  T copy{};
  taskENTER_CRITICAL(&_mux);
  copy = _value;
  taskEXIT_CRITICAL(&_mux);
  *value = std::move(copy);
}

template <typename T>
void BLEValueReceiver<T>::onValueChanged(const OnValueChanged<T>& callback) {
  _onValueChangedCallback = callback;
  _callbackSet.store(static_cast<bool>(callback), std::memory_order_release);
}

template <typename T>
void BLEValueReceiver<T>::_dispatchValueChanged() {
  // clear first: changes arriving while the callback runs schedule a new dispatch
  _callbackPending.store(false);

  T copy{};
  taskENTER_CRITICAL(&_mux);
  copy = _value;
  taskEXIT_CRITICAL(&_mux);

  if (_onValueChangedCallback) {
    _onValueChangedCallback(copy);
  }
}

template <typename T>
void BLEValueReceiver<T>::_handleNotify(NimBLERemoteCharacteristic* pChar,
                                        uint8_t* pData,
                                        size_t dataLen,
                                        bool /*isNotify*/) {
  BLEGC_LOGV("Received a notification. %s", blegc::remoteCharToStr(pChar).c_str());

  // Decode outside the lock. Only the host task writes _value, so read-modify-write is safe.
  T next{};
  taskENTER_CRITICAL(&_mux);
  next = _value;
  taskEXIT_CRITICAL(&_mux);

  const auto result = next.decode(pData, dataLen);
  if (result == BLEDecodeResult::NotSupported) {
    BLEGC_LOGV("Report not supported");
    return;
  }
  if (result == BLEDecodeResult::InvalidReport) {
    BLEGC_LOGE("Invalid report, length: %d", static_cast<int>(dataLen));
    return;
  }

#if CONFIG_BT_BLEGC_LOG_BUFFER_ENABLED
  next.reportData = std::shared_ptr<uint8_t[]>(new uint8_t[dataLen]);
  next.reportDataLen = dataLen;
  next.reportDataCap = dataLen;
  memcpy(next.reportData.get(), pData, dataLen);
#endif

  taskENTER_CRITICAL(&_mux);
  const bool changed = next != _value;
  std::swap(_value, next);
  taskEXIT_CRITICAL(&_mux);

  // at most one queued message per receiver; the callback always reads the latest value
  if (changed && _callbackSet.load(std::memory_order_acquire) && !_callbackPending.exchange(true)) {
    if (!blegc::internal::postUserCallback({BLEUserCallbackKind::ValueChanged, nullptr, this}, 0)) {
      _callbackPending.store(false);
    }
  }
}

template class BLEValueReceiver<XboxControlsState>;
template class BLEValueReceiver<XboxBatteryState>;
template class BLEValueReceiver<SteamControlsState>;
