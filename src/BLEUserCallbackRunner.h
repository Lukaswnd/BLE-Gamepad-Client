#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "messages.h"

/// Single task that runs every user callback (connection, scan and value callbacks).
class BLEUserCallbackRunner {
 public:
  BLEUserCallbackRunner() = delete;

  static bool start();
  static bool post(const BLEUserCallback& msg, TickType_t wait);

 private:
  static void _taskFn(void* pvParameters);

  static QueueHandle_t _queue;
  static TaskHandle_t _task;
};
