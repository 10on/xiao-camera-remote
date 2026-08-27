#pragma once

#include <NimBLEDevice.h>
#include "devices/device.h"

// Owns the single BLE central scan shared by every device driver — NimBLE
// only supports one active scan at a time, so individual Device instances
// don't scan themselves. Devices register once at startup; Device::
// activate()/deactivate() just flips a want-connection flag on the
// device, and this manager (re)scans/(re)connects for every
// active-but-disconnected device, stopping once all active devices are
// connected and resuming automatically if one drops.
class BleManager : public NimBLEScanCallbacks {
public:
	void begin();
	void registerDevice(Device *dev);
	void update();

	void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override;
	void onScanEnd(const NimBLEScanResults &results, int reason) override;

private:
	bool anyDeviceWaiting() const;
	void ensureScanning();

	static const size_t kMaxDevices = 4;
	Device *_devices[kMaxDevices] = {};
	size_t _deviceCount = 0;
};

extern BleManager bleManager;
