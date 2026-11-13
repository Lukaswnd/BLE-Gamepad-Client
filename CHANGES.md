# BLE-Gamepad-Client – low-overhead rework

Drop-in replacement: copy the files from `src/` over the original `src/` folder.
Files that are **not** in this archive stay unchanged (BLEBaseController.h, BLEBaseValue.*, BLEDeviceInfo.h,
utils.h, XboxController.h, SteamController.h, all *State/*Command structs except SteamControlsState.cpp).

The public API is the same: `begin()`, `end()`, `read()`, `write()`, `isConnected()`, `isConnecting()`,
`disconnect()`, `getAddress()`, `getLastAddress()`, `getAllowedAddress()`, `onConnecting/onConnectionFailed/
onConnected/onDisconnected/onValueChanged`, `BLEGamepadClient::init/enableDebugLog/getAutoScan`, and
`BLEAutoScan::enable/disable/isEnabled/isScanning/notify/onScanStarted/onScanStopped`.

## Threading model

| | Before | After |
|---|---|---|
| Global tasks | 3 (auto-scan, client events, user callbacks) | 2 (`blegc` manager, `blegcCb` callbacks) |
| Tasks per Xbox controller | 3 (2x receiver, 1x writer) | 0 |
| Stack, 1 Xbox controller | ~20 KB | ~7 KB |
| Stack, 3 Xbox controllers | ~38 KB | ~7 KB |
| Task creation | static constructors, before `setup()` | first `begin()` |
| Priority | 0 (same as idle) | manager 2, callbacks 1 (configurable) |

* `blegc` (manager): handles NimBLE client events, connect/disconnect/deregister requests and the
  auto-scan state machine. Scan triggers are task-notification bits, so they can never be lost.
* `blegcCb` (callbacks): runs every user callback, including `onValueChanged`. Each receiver has a
  "pending" flag, so at most one value message per receiver is queued and the callback always gets the
  latest state.
* `BLEValueReceiver`: no task, no heap mutex. The state is guarded by a spinlock; the notify handler
  decodes outside the lock.
* `BLEValueWriter`: no task. `write()` encodes and sends directly with write-without-response
  (non-blocking in NimBLE). Static mutex, 16-byte inline buffer, grows on the heap only if needed.

## Bug fixes

* Manager task no longer exits (and aborts) on events for unknown controllers.
* Writer: characteristic pointer cleared in `deinit()` before the client is deleted (use-after-free).
* Writer: no more buffer swapping or stale/uninitialised data being sent.
* `SteamControlsState::decode`: operator precedence fixed, every section is bounds-checked.
* `getManufacturerId()`: no `configASSERT` on foreign advertising data (remote reboot).
* `registerController()`: no double registration when `begin()` is called twice.
* Locks are no longer inside `configASSERT(...)`.
* Value callbacks are not delivered several times with the same state.
* Shared state (`_connectionState`, `_pClient`, `_lastAddress`, `_enabled`) is atomic.
* `disconnect()` and `end()` run on the manager task (no race with client deletion).
* Events are matched by `NimBLEClient*` instead of address (works with resolvable private addresses).
* Only one connection attempt at a time (NimBLE supports one pending connect); repeated
  advertisements cannot allocate two controller slots for the same device.
* Scan started/stopped callbacks are de-duplicated (one callback per real state change).
* Auto-scan resumes the remaining high/low-duty time after an interrupted scan.
* Fail-safe: values returned by `read()` are reset to defaults on disconnect.
* Logging: arguments are only evaluated when the level is enabled (no string building in the BLE host
  task when logging is off). `enableDebugLog()` still works at runtime.
* Device info and report map are read only when debug logging is enabled (faster connect).
* `_readDeviceInfo()`: skips non-readable characteristics.

## New configuration options (optional)

| Define | Default | Description |
|---|---|---|
| `CONFIG_BT_BLEGC_TASK_STACK_SIZE` | `4096` | Manager task stack (bytes) |
| `CONFIG_BT_BLEGC_TASK_PRIORITY` | `2` | Manager task priority |
| `CONFIG_BT_BLEGC_CALLBACK_TASK_STACK_SIZE` | `3072` | Callback task stack; raise if your callbacks use a lot of stack |
| `CONFIG_BT_BLEGC_CALLBACK_TASK_PRIORITY` | `1` | Callback task priority (same as Arduino `loop()`) |
| `CONFIG_BT_BLEGC_EVENT_QUEUE_LEN` | `16` | Internal event queue length |
| `CONFIG_BT_BLEGC_CALLBACK_QUEUE_LEN` | `16` | User callback queue length |
| `CONFIG_BT_BLEGC_INIT_ATTEMPTS` | `2` | Attempts to initialise a controller after bonding |

## Notes

* Register callbacks before or right after `begin()`; do not replace them while connected.
* Keep user callbacks short; all of them share one task.
* Controller objects must outlive their connection (globals, as in the examples, are fine).
* Recommended CI addition: `-Wall -Wextra` in `build_flags`.
