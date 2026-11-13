#pragma once

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>
#include "BLEAbstractController.h"
#include "messages.h"

class BLEControllerRegistry {
 public:
  struct AllocationInfo {
    unsigned int allocated = 0;     // controllers with a device assigned
    unsigned int notAllocated = 0;  // free controllers waiting for a device
    unsigned int pending = 0;       // allocated but not yet connected (connect in progress)
  };

  BLEControllerRegistry();

  BLEControllerRegistry(const BLEControllerRegistry&) = delete;
  BLEControllerRegistry& operator=(const BLEControllerRegistry&) = delete;

  void registerController(BLEAbstractController* pCtrl);
  AllocationInfo getAllocationInfo() const;

  // Called from the NimBLE host task (scan result). Cheap: match + allocate + post event.
  void tryConnectController(const NimBLEAdvertisedDevice* pAdvertisedDevice);

  // Called from the manager task only.
  void handleEvent(const BLEClientEvent& event);

 private:
  class ClientCallbacksImpl final : public NimBLEClientCallbacks {
   public:
    void onConnect(NimBLEClient* pClient) override;
    void onConnectFail(NimBLEClient* pClient, int reason) override;
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEClient* pClient, int reason) override;
  };

  BLEAbstractController* _findByClient(const NimBLEClient* pClient) const;
  BLEAbstractController* _findAndAllocateController(const NimBLEAdvertisedDevice* pAdvertisedDevice);
  void _connect(BLEAbstractController* pCtrl);
  void _initController(BLEAbstractController* pCtrl, NimBLEClient* pClient);
  void _teardown(BLEAbstractController* pCtrl);
  void _deregister(BLEAbstractController* pCtrl);
  void _remove(BLEAbstractController* pCtrl);
  void _lock() const;
  void _unlock() const;

  std::vector<BLEAbstractController*> _controllers;
  StaticSemaphore_t _mutexBuffer;
  SemaphoreHandle_t _mutex;
  ClientCallbacksImpl _clientCallbacks;
};
