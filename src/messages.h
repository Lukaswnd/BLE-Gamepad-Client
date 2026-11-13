#pragma once

#include <cstdint>

class BLEAbstractController;
class BLEValueReceiverBase;
class NimBLEClient;

/// Messages executed by the user callback task.
enum class BLEUserCallbackKind : uint8_t {
  ControllerConnecting = 1,
  ControllerConnectionFailed = 2,
  ControllerConnected = 3,
  ControllerDisconnected = 4,

  ScanStarted = 5,
  ScanStopped = 6,

  ValueChanged = 7,
};

struct BLEUserCallback {
  BLEUserCallbackKind kind;
  BLEAbstractController* pCtrl = nullptr;
  BLEValueReceiverBase* pReceiver = nullptr;
};

/// Events executed by the manager task.
enum class BLEClientEventKind : uint8_t {
  ConnectRequest = 0,     // pCtrl: allocated controller, connect to its address
  Connected = 1,          // pClient
  ConnectionFailed = 2,   // pClient
  Bonded = 3,             // pClient
  BondingFailed = 4,      // pClient
  Disconnected = 5,       // pClient
  DisconnectRequest = 6,  // pCtrl: user called disconnect()
  DeregisterRequest = 7,  // pCtrl: user called end()
};

struct BLEClientEvent {
  BLEClientEventKind kind;
  BLEAbstractController* pCtrl = nullptr;
  NimBLEClient* pClient = nullptr;
};

namespace blegc {
// Task notification bits of the manager task. Bits accumulate, so no scan trigger is ever lost.
constexpr uint32_t kNotifyClientEvent = 1u << 0;
constexpr uint32_t kNotifyScanAuto = 1u << 1;
constexpr uint32_t kNotifyScanEnabled = 1u << 2;
constexpr uint32_t kNotifyScanDisabled = 1u << 3;
constexpr uint32_t kNotifyScanFinished = 1u << 4;
}  // namespace blegc
