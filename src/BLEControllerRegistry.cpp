#include "BLEControllerRegistry.h"

#include <NimBLEDevice.h>
#include <algorithm>
#include "BLEInternal.h"
#include "config.h"
#include "logger.h"

namespace {

void postUser(BLEUserCallbackKind kind, BLEAbstractController* pCtrl) {
  blegc::internal::postUserCallback({kind, pCtrl, nullptr}, pdMS_TO_TICKS(20));
}

void notifyScan() {
  blegc::internal::notifyManager(blegc::kNotifyScanAuto);
}

}  // namespace

BLEControllerRegistry::BLEControllerRegistry() : _controllers(), _mutexBuffer(), _mutex(nullptr), _clientCallbacks() {
  _mutex = xSemaphoreCreateMutexStatic(&_mutexBuffer);  // no heap, safe before the scheduler starts
  _controllers.reserve(CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
}

void BLEControllerRegistry::_lock() const {
  xSemaphoreTake(_mutex, portMAX_DELAY);
}

void BLEControllerRegistry::_unlock() const {
  xSemaphoreGive(_mutex);
}

void BLEControllerRegistry::registerController(BLEAbstractController* pCtrl) {
  if (!pCtrl) {
    return;
  }

  bool added = false;
  _lock();
  if (std::find(_controllers.begin(), _controllers.end(), pCtrl) == _controllers.end()) {
    _controllers.push_back(pCtrl);
    added = true;
  }
  _unlock();

  if (!added) {
    BLEGC_LOGD("Controller already registered");
    return;
  }

  BLEGC_LOGD("Controller registered");
  notifyScan();
}

BLEControllerRegistry::AllocationInfo BLEControllerRegistry::getAllocationInfo() const {
  AllocationInfo result;

  _lock();
  for (const auto* pCtrl : _controllers) {
    if (pCtrl->isAllocated()) {
      result.allocated++;
      if (!pCtrl->isConnected()) {
        result.pending++;
      }
    } else if (!pCtrl->isPendingDeregistration()) {
      result.notAllocated++;
    }
  }
  _unlock();

  return result;
}

void BLEControllerRegistry::tryConnectController(const NimBLEAdvertisedDevice* pAdvertisedDevice) {
  auto* pCtrl = _findAndAllocateController(pAdvertisedDevice);
  if (!pCtrl) {
    return;
  }

  BLEGC_LOGI("Found controller, address: %s", std::string(pAdvertisedDevice->getAddress()).c_str());

  if (!blegc::internal::postClientEvent({BLEClientEventKind::ConnectRequest, pCtrl, nullptr})) {
    pCtrl->tryDeallocate();
  }
}

BLEAbstractController* BLEControllerRegistry::_findAndAllocateController(
    const NimBLEAdvertisedDevice* pAdvertisedDevice) {
  const auto address = pAdvertisedDevice->getAddress();

  BLEAbstractController* pLastMatch = nullptr;  // was connected to this device before
  BLEAbstractController* pFresh = nullptr;      // never connected
  BLEAbstractController* pOther = nullptr;      // was connected to another device
  unsigned int allocated = 0;
  bool busy = false;

  _lock();
  for (auto* pCtrl : _controllers) {
    if (pCtrl->isAllocated()) {
      allocated++;
      // one connection attempt at a time, and never two slots for the same device
      if (!pCtrl->isConnected() || pCtrl->getAddress() == address) {
        busy = true;
        break;
      }
      continue;
    }

    if (pCtrl->isPendingDeregistration()) {
      continue;
    }

    const auto allowed = pCtrl->getAllowedAddress();
    if (!allowed.isNull() && allowed != address) {
      continue;
    }

    if (!pCtrl->isSupported(pAdvertisedDevice)) {
      continue;
    }

    const auto last = pCtrl->getLastAddress();
    if (last.isNull()) {
      if (!pFresh) pFresh = pCtrl;
    } else if (last == address) {
      if (!pLastMatch) pLastMatch = pCtrl;
    } else if (!pOther) {
      pOther = pCtrl;
    }
  }
  _unlock();

  if (busy || allocated >= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
    return nullptr;
  }

  for (auto* pCtrl : {pLastMatch, pFresh, pOther}) {
    if (pCtrl && pCtrl->tryAllocate(address)) {
      return pCtrl;
    }
  }

  return nullptr;
}

BLEAbstractController* BLEControllerRegistry::_findByClient(const NimBLEClient* pClient) const {
  if (!pClient) {
    return nullptr;
  }

  BLEAbstractController* result = nullptr;
  _lock();
  for (auto* pCtrl : _controllers) {
    if (pCtrl->getClient() == pClient) {
      result = pCtrl;
      break;
    }
  }
  _unlock();
  return result;
}

void BLEControllerRegistry::handleEvent(const BLEClientEvent& event) {
  BLEGC_LOGD("Handling event kind: %d", static_cast<int>(event.kind));

  switch (event.kind) {
    case BLEClientEventKind::ConnectRequest:
      if (event.pCtrl) _connect(event.pCtrl);
      return;

    case BLEClientEventKind::DisconnectRequest: {
      auto* pClient = event.pCtrl ? event.pCtrl->getClient() : nullptr;
      if (pClient && pClient->isConnected()) {
        pClient->disconnect();
      }
      return;
    }

    case BLEClientEventKind::DeregisterRequest:
      if (event.pCtrl) _deregister(event.pCtrl);
      return;

    default:
      break;
  }

  auto* pCtrl = _findByClient(event.pClient);
  if (!pCtrl) {
    BLEGC_LOGD("Event for unknown client ignored");
    return;  // stale event (client already torn down)
  }

  switch (event.kind) {
    case BLEClientEventKind::Connected:
      if (pCtrl->isPendingDeregistration()) {
        event.pClient->disconnect();
      } else if (!event.pClient->secureConnection(true)) {  // async
        BLEGC_LOGE("Failed to initiate secure connection");
        event.pClient->disconnect();
      }
      break;

    case BLEClientEventKind::Bonded:
      if (!pCtrl->isConnected()) {  // ignore re-encryption of an already initialized controller
        _initController(pCtrl, event.pClient);
      }
      break;

    case BLEClientEventKind::BondingFailed:
      event.pClient->disconnect();
      break;

    case BLEClientEventKind::ConnectionFailed:
    case BLEClientEventKind::Disconnected:
      _teardown(pCtrl);
      break;

    default:
      break;
  }
}

void BLEControllerRegistry::_connect(BLEAbstractController* pCtrl) {
  if (!pCtrl->isAllocated() || pCtrl->getClient()) {
    return;
  }

  if (pCtrl->isPendingDeregistration()) {
    pCtrl->tryDeallocate();
    _remove(pCtrl);
    notifyScan();
    return;
  }

  const auto address = pCtrl->getAddress();
  auto* pClient = NimBLEDevice::createClient(address);
  if (!pClient) {
    BLEGC_LOGE("Failed to create client, address: %s", std::string(address).c_str());
    pCtrl->tryDeallocate();
    notifyScan();
    return;
  }

  pClient->setSelfDelete(false, false);
  pClient->setConnectTimeout(CONFIG_BT_BLEGC_CONN_TIMEOUT_MS);
  pClient->setClientCallbacks(&_clientCallbacks, false);

  pCtrl->setClient(pClient);
  pCtrl->markConnecting();
  postUser(BLEUserCallbackKind::ControllerConnecting, pCtrl);

  BLEGC_LOGI("Connecting, address: %s", std::string(address).c_str());

  // deleteAttributes = true, async = true, exchangeMTU = true. Stops a running scan internally.
  if (!pClient->connect(true, true, true)) {
    BLEGC_LOGE("Failed to initiate connection, address: %s", std::string(address).c_str());
    _teardown(pCtrl);
  }
}

void BLEControllerRegistry::_initController(BLEAbstractController* pCtrl, NimBLEClient* pClient) {
  if (pCtrl->isPendingDeregistration()) {
    pClient->disconnect();
    return;
  }

  bool ok = false;
  for (int attempt = 0; attempt < CONFIG_BT_BLEGC_INIT_ATTEMPTS && !ok; ++attempt) {
    ok = pCtrl->hidInit() && pCtrl->init();
  }

  if (!ok) {
    BLEGC_LOGW("Failed to initialize controller");
    pClient->disconnect();
    return;
  }

  pCtrl->markConnected();
  BLEGC_LOGI("Controller connected and initialized");
  postUser(BLEUserCallbackKind::ControllerConnected, pCtrl);
  notifyScan();  // fill remaining slots
}

void BLEControllerRegistry::_teardown(BLEAbstractController* pCtrl) {
  const auto prevState = pCtrl->markDisconnected();

  // deinit() first: it clears characteristic pointers before the client (and its attributes) is deleted
  if (!pCtrl->deinit()) {
    BLEGC_LOGW("Failed to deinitialize controller");
  }

  auto* pClient = pCtrl->getClient();
  pCtrl->setClient(nullptr);
  if (pClient && !NimBLEDevice::deleteClient(pClient)) {
    BLEGC_LOGE("Failed to delete client");
  }

  pCtrl->tryDeallocate();

  if (prevState == BLEAbstractController::ConnectionState::Connected) {
    postUser(BLEUserCallbackKind::ControllerDisconnected, pCtrl);
  } else if (prevState == BLEAbstractController::ConnectionState::Connecting) {
    postUser(BLEUserCallbackKind::ControllerConnectionFailed, pCtrl);
  }

  if (pCtrl->isPendingDeregistration()) {
    _remove(pCtrl);
  }

  notifyScan();
}

void BLEControllerRegistry::_deregister(BLEAbstractController* pCtrl) {
  if (!pCtrl->isPendingDeregistration()) {
    return;
  }

  auto* pClient = pCtrl->getClient();
  if (pClient) {
    // deregistration completes in _teardown() after the disconnect / connect failure event
    if (pClient->isConnected()) {
      pClient->disconnect();
    } else if (!pClient->cancelConnect()) {
      BLEGC_LOGD("Cancel connect command failed");
    }
    return;
  }

  if (pCtrl->isAllocated()) {
    return;  // ConnectRequest still queued, _connect() completes the deregistration
  }

  _remove(pCtrl);
  notifyScan();
}

void BLEControllerRegistry::_remove(BLEAbstractController* pCtrl) {
  _lock();
  _controllers.erase(std::remove(_controllers.begin(), _controllers.end(), pCtrl), _controllers.end());
  _unlock();

  pCtrl->markCompletedDeregistration();
  BLEGC_LOGD("Controller deregistered");
}

// ---- NimBLE callbacks (host task): only post events, never block ----

void BLEControllerRegistry::ClientCallbacksImpl::onConnect(NimBLEClient* pClient) {
  BLEGC_LOGD("Connected to a device, address: %s", std::string(pClient->getPeerAddress()).c_str());
  blegc::internal::postClientEvent({BLEClientEventKind::Connected, nullptr, pClient});
}

void BLEControllerRegistry::ClientCallbacksImpl::onConnectFail(NimBLEClient* pClient, int reason) {
  BLEGC_LOGW("Failed connecting to a device, reason: 0x%04x %s", reason, NimBLEUtils::returnCodeToString(reason));
  blegc::internal::postClientEvent({BLEClientEventKind::ConnectionFailed, nullptr, pClient});
}

void BLEControllerRegistry::ClientCallbacksImpl::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
  auto* pClient = NimBLEDevice::getClientByHandle(connInfo.getConnHandle());
  if (!pClient) {
    BLEGC_LOGW("Authentication complete for unknown connection");
    return;
  }

  if (connInfo.isBonded()) {
    BLEGC_LOGI("Bonded successfully with a device, address: %s", std::string(connInfo.getAddress()).c_str());
    blegc::internal::postClientEvent({BLEClientEventKind::Bonded, nullptr, pClient});
  } else {
    BLEGC_LOGW("Failed to bond with a device, address: %s", std::string(connInfo.getAddress()).c_str());
    blegc::internal::postClientEvent({BLEClientEventKind::BondingFailed, nullptr, pClient});
  }
}

void BLEControllerRegistry::ClientCallbacksImpl::onDisconnect(NimBLEClient* pClient, int reason) {
  BLEGC_LOGI("Device disconnected, reason: 0x%04x %s", reason, NimBLEUtils::returnCodeToString(reason));
  blegc::internal::postClientEvent({BLEClientEventKind::Disconnected, nullptr, pClient});
}
