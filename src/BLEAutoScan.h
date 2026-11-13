#pragma once

#include <NimBLEDevice.h>
#include <atomic>
#include <cstdint>
#include <functional>

class BLEAutoScan {
 public:
  BLEAutoScan();
  ~BLEAutoScan() = default;

  BLEAutoScan(const BLEAutoScan&) = delete;
  BLEAutoScan& operator=(const BLEAutoScan&) = delete;

  void enable();
  void disable();
  bool isEnabled() const;
  bool isScanning() const;
  void notify() const;
  void onScanStarted(const std::function<void()>& callback);
  void onScanStopped(const std::function<void()>& callback);

  friend class BLEUserCallbackRunner;

  // ---- internal, used by the library ----
  void attach();               // registers scan callbacks (after NimBLE init)
  void handle(uint32_t bits);  // manager task only

 private:
  class ScanCallbacksImpl final : public NimBLEScanCallbacks {
   public:
    void onResult(const NimBLEAdvertisedDevice* pAdvertisedDevice) override;
    void onScanEnd(const NimBLEScanResults& results, int reason) override;
  };

  void callOnScanStarted();
  void callOnScanStopped();
  void _startScan(NimBLEScan* pScan, bool highDuty, uint32_t durationMs);
  void _syncState(NimBLEScan* pScan);

  std::atomic_bool _enabled{true};
  bool _attached{false};
  bool _scanActive{false};
  uint32_t _startTimeMs{0};
  ScanCallbacksImpl _scanCallbacks;
  std::function<void()> _onScanStarted;
  std::function<void()> _onScanStopped;
};
