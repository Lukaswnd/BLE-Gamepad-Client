#include "BLEUserCallbackRunner.h"

#include "BLEAbstractController.h"
#include "BLEAutoScan.h"
#include "BLEInternal.h"
#include "BLEValueReceiver.h"
#include "config.h"
#include "logger.h"

QueueHandle_t BLEUserCallbackRunner::_queue = nullptr;
TaskHandle_t BLEUserCallbackRunner::_task = nullptr;

bool BLEUserCallbackRunner::start() {
  if (_queue) {
    return true;
  }

  _queue = xQueueCreate(CONFIG_BT_BLEGC_CALLBACK_QUEUE_LEN, sizeof(BLEUserCallback));
  if (!_queue) {
    return false;
  }

  if (xTaskCreate(_taskFn, "blegcCb", CONFIG_BT_BLEGC_CALLBACK_TASK_STACK_SIZE, nullptr,
                  CONFIG_BT_BLEGC_CALLBACK_TASK_PRIORITY, &_task) != pdPASS) {
    vQueueDelete(_queue);
    _queue = nullptr;
    return false;
  }

  return true;
}

bool BLEUserCallbackRunner::post(const BLEUserCallback& msg, const TickType_t wait) {
  if (!_queue) {
    return false;
  }

  if (xQueueSend(_queue, &msg, wait) != pdTRUE) {
    BLEGC_LOGW("User callback queue full, callback %d dropped", static_cast<int>(msg.kind));
    return false;
  }
  return true;
}

void BLEUserCallbackRunner::_taskFn(void*) {
  for (;;) {
    BLEUserCallback msg{};
    if (xQueueReceive(_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    switch (msg.kind) {
      case BLEUserCallbackKind::ControllerConnecting:
        if (msg.pCtrl) msg.pCtrl->callOnConnecting();
        break;
      case BLEUserCallbackKind::ControllerConnectionFailed:
        if (msg.pCtrl) msg.pCtrl->callOnConnectionFailed();
        break;
      case BLEUserCallbackKind::ControllerConnected:
        if (msg.pCtrl) msg.pCtrl->callOnConnected();
        break;
      case BLEUserCallbackKind::ControllerDisconnected:
        if (msg.pCtrl) msg.pCtrl->callOnDisconnected();
        break;
      case BLEUserCallbackKind::ScanStarted:
        blegc::internal::autoScan().callOnScanStarted();
        break;
      case BLEUserCallbackKind::ScanStopped:
        blegc::internal::autoScan().callOnScanStopped();
        break;
      case BLEUserCallbackKind::ValueChanged:
        if (msg.pReceiver) msg.pReceiver->_dispatchValueChanged();
        break;
    }
  }
}
