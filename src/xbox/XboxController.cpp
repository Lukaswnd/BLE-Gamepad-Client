#include "XboxController.h"

#include <NimBLEDevice.h>
#include "BLEValueReceiver.h"
#include "logger.h"
#include "utils.h"

using namespace blegc;

XboxController::XboxController(const NimBLEAddress& allowedAddress) : BLEBaseController(allowedAddress) {}
XboxController::XboxController(const std::string& allowedAddress)
    : XboxController(NimBLEAddress(allowedAddress, addrTypePublic)) {}
XboxController::XboxController() : XboxController(NimBLEAddress()) {}
XboxController::~XboxController() = default;

bool XboxController::isSupported(const NimBLEAdvertisedDevice* pAdvertisedDevice) {
  if (pAdvertisedDevice->haveName()) {
    if (pAdvertisedDevice->getName() == "Xbox Wireless Controller") {
      return true;
    }

    BLEGC_LOGV("Name mismatch: %s", pAdvertisedDevice->getName().c_str());
    return false;
  }

  if (!pAdvertisedDevice->haveAppearance() || pAdvertisedDevice->getAppearance() != gamepadAppearance) {
    return false;
  }

  if (!pAdvertisedDevice->haveManufacturerData()) {
    return false;
  }

  return getManufacturerId(pAdvertisedDevice) == microsoftCompanyId;
}

bool XboxController::deinit() {
  // clear the writer first: its characteristic pointer becomes invalid when the client is deleted
  BLEValueWriter<XboxVibrationsCommand>::reset();
  BLEValueReceiver<XboxControlsState>::reset();
  BLEValueReceiver<XboxBatteryState>::reset();
  return true;
}

bool XboxController::init() {
  auto* pClient = getClient();
  if (!pClient) {
    return false;
  }

  auto* pControlsChar = findNotifiableCharacteristic(pClient, hidSvcUUID, inputReportChrUUID);
  auto* pBatteryChar = findNotifiableCharacteristic(pClient, batterySvcUUID, batteryLevelCharUUID);
  auto* pVibrationsChar = findWritableCharacteristic(pClient, hidSvcUUID, inputReportChrUUID);

  return BLEValueReceiver<XboxControlsState>::init(pControlsChar) &&
         BLEValueReceiver<XboxBatteryState>::init(pBatteryChar) &&
         BLEValueWriter<XboxVibrationsCommand>::init(pVibrationsChar);
}
