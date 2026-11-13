#include "BLEAutoScan.h"

#include <NimBLEDevice.h>
#include "BLEControllerRegistry.h"
#include "BLEInternal.h"
#include "config.h"
#include "logger.h"
#include "messages.h"

BLEAutoScan::BLEAutoScan() : _scanCallbacks(), _onScanStarted([]() {}), _onScanStopped([]() {}) {}

/**
 * @brief Enables the auto-scan feature (enabled by default).
 *
 * Auto-scan automatically starts scanning whenever there are one or more controller instances that have been
 * initialized with `begin()` but are not yet connected. Scanning stops automatically once all controller instances
 * are connected.
 */
void BLEAutoScan::enable() {
  if (!_enabled.exchange(true)) {
    blegc::internal::notifyManager(blegc::kNotifyScanEnabled);
  }
}

/**
 * @brief Disables the auto-scan feature.
 */
void BLEAutoScan::disable() {
  if (_enabled.exchange(false)) {
    blegc::internal::notifyManager(blegc::kNotifyScanDisabled);
  }
}

bool BLEAutoScan::isEnabled() const {
  return _enabled.load();
}

bool BLEAutoScan::isScanning() const {
  return NimBLEDevice::isInitialized() && NimBLEDevice::getScan()->isScanning();
}

/**
 * @brief Triggers auto-scan check. If previous scan ended without filling all available connection slots, you can
 * start another scan by calling this method.
 */
void BLEAutoScan::notify() const {
  blegc::internal::notifyManager(blegc::kNotifyScanAuto);
}

void BLEAutoScan::onScanStarted(const std::function<void()>& callback) {
  _onScanStarted = callback ? callback : []() {};
}

void BLEAutoScan::onScanStopped(const std::function<void()>& callback) {
  _onScanStopped = callback ? callback : []() {};
}

void BLEAutoScan::callOnScanStarted() {
  _onScanStarted();
}

void BLEAutoScan::callOnScanStopped() {
  _onScanStopped();
}

void BLEAutoScan::attach() {
  if (_attached || !NimBLEDevice::isInitialized()) {
    return;
  }

  auto* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&_scanCallbacks, false);
  pScan->setMaxResults(0);  // don't store results, saves RAM
  _attached = true;
}

void BLEAutoScan::_startScan(NimBLEScan* pScan, const bool highDuty, const uint32_t durationMs) {
  if (highDuty) {
    pScan->setWindow(CONFIG_BT_BLEGC_HIGH_DUTY_SCAN_WINDOW_MS);
    pScan->setInterval(CONFIG_BT_BLEGC_HIGH_DUTY_SCAN_INTERVAL_MS);
    pScan->setActiveScan(CONFIG_BT_BLEGC_HIGH_DUTY_SCAN_ACTIVE > 0);
  } else {
    pScan->setWindow(CONFIG_BT_BLEGC_LOW_DUTY_SCAN_WINDOW_MS);
    pScan->setInterval(CONFIG_BT_BLEGC_LOW_DUTY_SCAN_INTERVAL_MS);
    pScan->setActiveScan(CONFIG_BT_BLEGC_LOW_DUTY_SCAN_ACTIVE > 0);
  }

  if (!pScan->start(durationMs, false, true)) {
    BLEGC_LOGE("Failed to start scan");
  }
}

void BLEAutoScan::_syncState(NimBLEScan* pScan) {
  // one user callback per real state change, no duplicates
  const bool scanning = pScan->isScanning();
  if (scanning != _scanActive) {
    _scanActive = scanning;
    blegc::internal::postUserCallback(
        {scanning ? BLEUserCallbackKind::ScanStarted : BLEUserCallbackKind::ScanStopped}, pdMS_TO_TICKS(20));
  }
}

void BLEAutoScan::handle(const uint32_t bits) {
  if (!NimBLEDevice::isInitialized()) {
    return;
  }

  auto* pScan = NimBLEDevice::getScan();
  constexpr uint32_t scanBits = blegc::kNotifyScanAuto | blegc::kNotifyScanEnabled | blegc::kNotifyScanDisabled |
                                blegc::kNotifyScanFinished;

  if (bits & scanBits) {
    constexpr uint32_t hdMs = CONFIG_BT_BLEGC_HIGH_DUTY_SCAN_DURATION_MS;
    constexpr uint32_t ldMs = CONFIG_BT_BLEGC_LOW_DUTY_SCAN_DURATION_MS;
    const bool scanning = pScan->isScanning();
    const char* decision = "no action";

    if (!_enabled.load()) {
      if ((bits & blegc::kNotifyScanDisabled) && scanning) {
        pScan->stop();
        decision = "stop scan";
      }
    } else if (!scanning) {
      const auto info = blegc::internal::registry().getAllocationInfo();
      const bool canScan =
          info.notAllocated > 0 && info.allocated < CONFIG_BT_NIMBLE_MAX_CONNECTIONS && info.pending == 0;

      if (canScan) {
        const uint32_t now = millis();
        if (bits & (blegc::kNotifyScanAuto | blegc::kNotifyScanEnabled)) {
          _startTimeMs = now;
          _startScan(pScan, true, hdMs);
          decision = "start high duty scan";
        } else if (bits & blegc::kNotifyScanFinished) {
          const uint32_t elapsed = now - _startTimeMs;
          if (elapsed < hdMs) {
            _startScan(pScan, true, hdMs - elapsed);  // resume interrupted high duty phase
            decision = "resume high duty scan";
          } else if (elapsed < hdMs + ldMs) {
            _startScan(pScan, false, hdMs + ldMs - elapsed);
            decision = "start low duty scan";
          }
        }
      }
    }

    BLEGC_LOGD("AutoScan bits: 0x%02x, enabled: %d, scanning: %d -> %s", static_cast<unsigned>(bits),
               _enabled.load(), scanning, decision);
  }

  _syncState(pScan);
}

// ---- NimBLE callbacks (host task) ----

void BLEAutoScan::ScanCallbacksImpl::onResult(const NimBLEAdvertisedDevice* pAdvertisedDevice) {
  BLEGC_LOGV("Device discovered, address: %s, name: %s", std::string(pAdvertisedDevice->getAddress()).c_str(),
             pAdvertisedDevice->getName().c_str());

  blegc::internal::registry().tryConnectController(pAdvertisedDevice);
}

void BLEAutoScan::ScanCallbacksImpl::onScanEnd(const NimBLEScanResults&, int reason) {
  BLEGC_LOGD("Scan ended, reason: 0x%04x", reason);
  blegc::internal::notifyManager(blegc::kNotifyScanFinished);
}
