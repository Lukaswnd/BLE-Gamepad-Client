#pragma once

#include <NimBLEDevice.h>
#include <atomic>
#include <cstdint>
#include "BLEDeviceInfo.h"

class BLEAbstractController {
 public:
  explicit BLEAbstractController(const NimBLEAddress& allowedAddress);
  virtual ~BLEAbstractController() = default;

  BLEAbstractController(const BLEAbstractController&) = delete;
  BLEAbstractController& operator=(const BLEAbstractController&) = delete;

  void begin();
  void end();
  NimBLEAddress getAddress() const;
  NimBLEAddress getLastAddress() const;
  NimBLEAddress getAllowedAddress() const;
  bool isConnected() const;
  bool isConnecting() const;
  void disconnect();

  friend class BLEUserCallbackRunner;
  friend class BLEControllerRegistry;

 protected:
  enum class ConnectionState : uint8_t {
    Connecting,
    Connected,
    Disconnected,
  };

  NimBLEClient* getClient() const;
  void setClient(NimBLEClient* pClient);
  bool tryAllocate(const NimBLEAddress& address);
  bool tryDeallocate();
  bool isAllocated() const;
  void markCompletedDeregistration();
  void markConnecting();
  void markConnected();
  ConnectionState markDisconnected();  // returns the previous state
  bool isPendingDeregistration() const;
  bool hidInit();

  virtual void callOnConnecting() = 0;
  virtual void callOnConnectionFailed() = 0;
  virtual void callOnConnected() = 0;
  virtual void callOnDisconnected() = 0;
  virtual bool isSupported(const NimBLEAdvertisedDevice* pAdvertisedDevice) = 0;
  virtual bool init() = 0;
  virtual bool deinit() = 0;

  static uint64_t _encodeAddress(const NimBLEAddress& address);
  static NimBLEAddress _decodeAddress(uint64_t address);
  void _readDeviceInfo();

  std::atomic_bool _pendingDeregistration{false};
  std::atomic<uint64_t> _address{0};
  std::atomic<NimBLEClient*> _pClient{nullptr};
  std::atomic<ConnectionState> _connectionState{ConnectionState::Disconnected};
  std::atomic<uint64_t> _lastAddress{0};
  NimBLEAddress _allowedAddress;
  BLEDeviceInfo _deviceInfo;
};
