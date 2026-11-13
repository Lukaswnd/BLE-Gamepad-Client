#pragma once

#include "BLEAutoScan.h"
#include "BLEControllerRegistry.h"

// export headers
#include "xbox/XboxController.h"
#include "steam/SteamController.h"

class BLEGamepadClient {
 public:
  BLEGamepadClient() = delete;

  static void init(bool deleteBonds = true);
  static void enableDebugLog();
  static BLEAutoScan* getAutoScan();
};
