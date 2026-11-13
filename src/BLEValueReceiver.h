#pragma once

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <atomic>
#include <functional>

template <typename T>
using OnValueChanged = std::function<void(T& value)>;

/// Non-template base so the single callback task can dispatch value callbacks of any type.
class BLEValueReceiverBase {
 public:
  virtual ~BLEValueReceiverBase() = default;
  friend class BLEUserCallbackRunner;

 protected:
  virtual void _dispatchValueChanged() = 0;
};

template <typename T>
class BLEValueReceiver : public BLEValueReceiverBase {
 public:
  BLEValueReceiver();
  ~BLEValueReceiver() override = default;

  /**
   * @brief Read the latest value from the connected controller.
   * @param[out] value Pointer to the value instance where the data will be written.
   */
  void read(T* value);

  /**
   * @brief Sets a callback that is invoked whenever the value changes.
   * @param callback The function to call when a new value is received that differs from the previous one.
   */
  void onValueChanged(const OnValueChanged<T>& callback);

 protected:
  bool init(NimBLERemoteCharacteristic* pChar);
  /// Resets the stored value to defaults (fail-safe on disconnect).
  void reset();
  void _dispatchValueChanged() override;

 private:
  void _handleNotify(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t dataLen, bool isNotify);

  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  T _value{};
  OnValueChanged<T> _onValueChangedCallback{};
  std::atomic_bool _callbackSet{false};
  std::atomic_bool _callbackPending{false};
};
