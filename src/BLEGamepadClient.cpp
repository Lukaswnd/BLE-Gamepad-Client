#include "BLEGamepadClient.h"

#include <NimBLEDevice.h>
#include <atomic>
#include "BLEInternal.h"
#include "BLEUserCallbackRunner.h"
#include "config.h"
#include "logger.h"

#if defined(CONFIG_BT_NIMBLE_PINNED_TO_CORE) && !defined(CONFIG_FREERTOS_UNICORE)
#define BLEGC_TASK_CORE CONFIG_BT_NIMBLE_PINNED_TO_CORE
#else
#define BLEGC_TASK_CORE tskNO_AFFINITY
#endif

namespace {

// 0 = not started, 1 = starting, 2 = running, 3 = failed
std::atomic<int> gState{0};
TaskHandle_t gManagerTask = nullptr;
QueueHandle_t gEventQueue = nullptr;

// Function-local statics: safe to use from global constructors (no static init order problems),
// constructors do not touch FreeRTOS tasks or NimBLE.
BLEControllerRegistry& registryInstance() {
  static BLEControllerRegistry instance;
  return instance;
}

BLEAutoScan& autoScanInstance() {
  static BLEAutoScan instance;
  return instance;
}

void managerTaskFn(void*) {
  auto& registry = registryInstance();
  auto& autoScan = autoScanInstance();

  for (;;) {
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);

    BLEClientEvent event{};
    while (xQueueReceive(gEventQueue, &event, 0) == pdTRUE) {
      registry.handleEvent(event);
    }

    autoScan.handle(bits);
  }
}

}  // namespace

namespace blegc {
namespace internal {

bool start() {
  int expected = 0;
  if (!gState.compare_exchange_strong(expected, 1)) {
    while (gState.load() == 1) {
      vTaskDelay(1);
    }
    return gState.load() == 2;
  }

  blegc::applyLogLevel();
  registryInstance();
  autoScanInstance();

  bool ok = true;
  gEventQueue = xQueueCreate(CONFIG_BT_BLEGC_EVENT_QUEUE_LEN, sizeof(BLEClientEvent));
  ok = ok && gEventQueue != nullptr;
  ok = ok && BLEUserCallbackRunner::start();
  ok = ok && xTaskCreatePinnedToCore(managerTaskFn, "blegc", CONFIG_BT_BLEGC_TASK_STACK_SIZE, nullptr,
                                     CONFIG_BT_BLEGC_TASK_PRIORITY, &gManagerTask, BLEGC_TASK_CORE) == pdPASS;

  if (!ok) {
    BLEGC_LOGE("Failed to start library tasks (out of memory?)");
  }

  gState.store(ok ? 2 : 3);
  return ok;
}

bool isStarted() {
  return gState.load() == 2;
}

void notifyManager(const uint32_t bits) {
  if (isStarted()) {
    xTaskNotify(gManagerTask, bits, eSetBits);
  }
}

bool postClientEvent(const BLEClientEvent& event) {
  if (!isStarted()) {
    return false;
  }

  if (xQueueSend(gEventQueue, &event, 0) != pdTRUE) {
    BLEGC_LOGE("Event queue full, event %d dropped", static_cast<int>(event.kind));
    return false;
  }

  xTaskNotify(gManagerTask, kNotifyClientEvent, eSetBits);
  return true;
}

bool postUserCallback(const BLEUserCallback& msg, const TickType_t wait) {
  return BLEUserCallbackRunner::post(msg, wait);
}

BLEControllerRegistry& registry() {
  return registryInstance();
}

BLEAutoScan& autoScan() {
  return autoScanInstance();
}

}  // namespace internal
}  // namespace blegc

/**
 * @brief Initializes the library.
 *
 * This method is called automatically; there is no need to invoke it explicitly in your application code.
 *
 * @param deleteBonds If true, deletes all stored bonding information.
 */
void BLEGamepadClient::init(const bool deleteBonds) {
  if (!blegc::internal::start()) {
    return;
  }

  if (!NimBLEDevice::isInitialized()) {
    BLEGC_LOGD("Initializing NimBLE");
    NimBLEDevice::init(CONFIG_BT_BLEGC_DEVICE_NAME);
    NimBLEDevice::setPower(CONFIG_BT_BLEGC_POWER_DBM);
    NimBLEDevice::setSecurityAuth(CONFIG_BT_BLEGC_SECURITY_AUTH);
    NimBLEDevice::setSecurityIOCap(CONFIG_BT_BLEGC_SECURITY_IO_CAP);

    if (deleteBonds) {
      NimBLEDevice::deleteAllBonds();
    }
  }

  autoScanInstance().attach();
}

/**
 * @brief Enables debug-level logging.
 */
void BLEGamepadClient::enableDebugLog() {
  blegc::setLogLevelDebug();
}

BLEAutoScan* BLEGamepadClient::getAutoScan() {
  return &autoScanInstance();
}
