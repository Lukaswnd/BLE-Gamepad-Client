#pragma once

#include <freertos/FreeRTOS.h>
#include "messages.h"

class BLEControllerRegistry;
class BLEAutoScan;

// Internal glue between the library components. Not part of the public API.
namespace blegc {
namespace internal {

/// Creates the queues and the two library tasks. Idempotent and thread safe.
bool start();
bool isStarted();

/// Sets notification bits on the manager task (any task context, non-blocking).
void notifyManager(uint32_t bits);

/// Queues an event for the manager task (any task context, non-blocking).
bool postClientEvent(const BLEClientEvent& event);

/// Queues a user callback for the callback task.
bool postUserCallback(const BLEUserCallback& msg, TickType_t wait = 0);

BLEControllerRegistry& registry();
BLEAutoScan& autoScan();

}  // namespace internal
}  // namespace blegc
