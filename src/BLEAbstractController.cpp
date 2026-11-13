#include "BLEAbstractController.h"

#include <NimBLEDevice.h>
#include "BLEControllerRegistry.h"
#include "BLEGamepadClient.h"
#include "BLEInternal.h"
#include "logger.h"
#include "utils.h"

namespace {
constexpr uint64_t kAddrMask = 0xFFFFFFFFFFFFULL;  // 48 bit
constexpr uint64_t kAddrValid = 1ULL << 63;         // distinguishes any real address from "not allocated" (0)
}  // namespace

BLEAbstractController::BLEAbstractController(const NimBLEAddress& allowedAddress) : _allowedAddress(allowedAddress) {}

void BLEAbstractController::begin() {
  BLEGamepadClient::init();
  if (!blegc::internal::isStarted()) {
    BLEGC_LOGE("Library not started, begin() aborted");
    return;
  }

  blegc::internal::registry().registerController(this);
}

void BLEAbstractController::end() {
  if (_pendingDeregistration.exchange(true)) {
    return;
  }

  if (!blegc::internal::postClientEvent({BLEClientEventKind::DeregisterRequest, this, nullptr})) {
    _pendingDeregistration.store(false);
  }
}

/**
 * @brief Returns the address of the currently connected controller. If controller is not connected a null address
 * is returned (a NimBLEAddress instance for which method `isNull()` returns true).
 */
NimBLEAddress BLEAbstractController::getAddress() const {
  return _decodeAddress(_address.load());
}

uint64_t BLEAbstractController::_encodeAddress(const NimBLEAddress& address) {
  if (address.isNull()) {
    return 0;
  }
  return (static_cast<uint64_t>(address) & kAddrMask) | (static_cast<uint64_t>(address.getType()) << 48) | kAddrValid;
}

NimBLEAddress BLEAbstractController::_decodeAddress(const uint64_t address) {
  if (address == 0) {
    return NimBLEAddress();
  }
  return NimBLEAddress(address & kAddrMask, static_cast<uint8_t>((address >> 48) & 0xFF));
}

bool BLEAbstractController::tryAllocate(const NimBLEAddress& address) {
  uint64_t expected = 0;
  return _address.compare_exchange_strong(expected, _encodeAddress(address));
}

bool BLEAbstractController::tryDeallocate() {
  const auto old = _address.exchange(0);
  if (old == 0) {
    return false;
  }
  _lastAddress.store(old);
  return true;
}

/**
 * @brief Is controller connected to the board and fully initialized.
 */
bool BLEAbstractController::isConnected() const {
  return _connectionState.load() == ConnectionState::Connected;
}

bool BLEAbstractController::isConnecting() const {
  return _connectionState.load() == ConnectionState::Connecting;
}

NimBLEAddress BLEAbstractController::getLastAddress() const {
  return _decodeAddress(_lastAddress.load());
}

NimBLEAddress BLEAbstractController::getAllowedAddress() const {
  return _allowedAddress;
}

bool BLEAbstractController::isAllocated() const {
  return _address.load() != 0;
}

void BLEAbstractController::markCompletedDeregistration() {
  _pendingDeregistration.store(false);
}

void BLEAbstractController::markConnecting() {
  _connectionState.store(ConnectionState::Connecting);
}

void BLEAbstractController::markConnected() {
  _connectionState.store(ConnectionState::Connected);
}

BLEAbstractController::ConnectionState BLEAbstractController::markDisconnected() {
  return _connectionState.exchange(ConnectionState::Disconnected);
}

bool BLEAbstractController::isPendingDeregistration() const {
  return _pendingDeregistration.load();
}

bool BLEAbstractController::hidInit() {
  // Device info and report map are only used for diagnostics. Skip the extra GATT reads unless debugging.
  if (!blegc::logEnabled(ESP_LOG_DEBUG)) {
    return true;
  }

  _readDeviceInfo();
  BLEGC_LOGD("%s", std::string(_deviceInfo).c_str());

#if CONFIG_BT_BLEGC_LOG_BUFFER_ENABLED
  std::vector<uint8_t> buffer;
  blegc::readReportMap(getClient(), &buffer);
  BLEGC_LOGD_BUFFER_HEX(buffer.data(), buffer.size());
#endif
  return true;
}

void BLEAbstractController::_readDeviceInfo() {
  auto* pClient = getClient();
  if (!pClient) {
    return;
  }

  auto* pService = pClient->getService(blegc::deviceInfoSvcUUID);
  if (!pService) {
    BLEGC_LOGD("Device info service not found");
    return;
  }

  auto characteristics = pService->getCharacteristics(false);
  if (characteristics.empty()) {
    characteristics = pService->getCharacteristics(true);
  }

  for (auto* pChar : characteristics) {
    if (!pChar->canRead()) {
      BLEGC_LOGD("Skipping non-readable characteristic, uuid: %s", std::string(pChar->getUUID()).c_str());
      continue;
    }

    const auto attValue = pChar->readValue();
    const auto& uuid = pChar->getUUID();
    if (uuid == blegc::manufacturerNameChrUUID) {
      _deviceInfo.manufacturerName.assign(attValue.c_str(), attValue.length());
    } else if (uuid == blegc::modelNameChrUUID) {
      _deviceInfo.modelName.assign(attValue.c_str(), attValue.length());
    } else if (uuid == blegc::serialNumberChrUUID) {
      _deviceInfo.serialNumber.assign(attValue.c_str(), attValue.length());
    } else if (uuid == blegc::firmwareRevisionChrUUID) {
      _deviceInfo.firmwareRevision.assign(attValue.c_str(), attValue.length());
    } else if (uuid == blegc::pnpIdChrUUID) {
      _deviceInfo.pnpId.assign(attValue.begin(), attValue.end());
    }
  }
}

NimBLEClient* BLEAbstractController::getClient() const {
  return _pClient.load();
}

void BLEAbstractController::setClient(NimBLEClient* pClient) {
  _pClient.store(pClient);
}

void BLEAbstractController::disconnect() {
  if (!isConnected()) {
    BLEGC_LOGD("Cannot disconnect controller that's not connected");
    return;
  }

  // Executed by the manager task, so it can never race with client deletion.
  blegc::internal::postClientEvent({BLEClientEventKind::DisconnectRequest, this, nullptr});
}
