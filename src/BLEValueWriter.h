#pragma once

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

template <typename T>
class BLEValueWriter {
 public:
  BLEValueWriter();
  virtual ~BLEValueWriter();

  BLEValueWriter(const BLEValueWriter&) = delete;
  BLEValueWriter& operator=(const BLEValueWriter&) = delete;

  /**
   * @brief Send the command to the connected controller. Non-blocking (write without response).
   * Ignored if the controller is not connected.
   * @param cmd Command to send.
   */
  void write(T& cmd);

 protected:
  bool init(NimBLERemoteCharacteristic* pChar);
  /// Must be called before the client is deleted.
  void reset();

 private:
  NimBLERemoteCharacteristic* _pChar;
  StaticSemaphore_t _mutexBuffer;
  SemaphoreHandle_t _mutex;
  uint8_t _inlineBuffer[16];
  uint8_t* _pBuffer;
  size_t _capacity;
};
