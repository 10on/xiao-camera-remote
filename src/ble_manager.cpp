#include "ble_manager.h"

BleManager bleManager;

void BleManager::begin() {
	NimBLEScan *scan = NimBLEDevice::getScan();
	scan->setScanCallbacks(this, false);
	scan->setActiveScan(true);
	scan->setInterval(100);
	scan->setWindow(100);
}

void BleManager::registerDevice(Device *dev) {
	if (_deviceCount < kMaxDevices) {
		_devices[_deviceCount++] = dev;
	}
}

bool BleManager::anyDeviceWaiting() const {
	for (size_t i = 0; i < _deviceCount; i++) {
		if (_devices[i]->isActive() && !_devices[i]->isConnected()) return true;
	}
	return false;
}

void BleManager::ensureScanning() {
	NimBLEScan *scan = NimBLEDevice::getScan();
	bool waiting = anyDeviceWaiting();
	if (waiting && !scan->isScanning()) {
		scan->start(0, false); // duration 0 = scan indefinitely until stop()
	} else if (!waiting && scan->isScanning()) {
		scan->stop();
	}
}

void BleManager::update() {
	for (size_t i = 0; i < _deviceCount; i++) {
		_devices[i]->tick();
	}
	ensureScanning();
}

void BleManager::onResult(const NimBLEAdvertisedDevice *advertisedDevice) {
	for (size_t i = 0; i < _deviceCount; i++) {
		Device *dev = _devices[i];
		if (dev->isActive() && !dev->isConnected() &&
		    dev->matchesAdvertisement(advertisedDevice)) {
			// Stop scanning while we connect — NimBLE handles one
			// connection attempt at a time; ensureScanning() resumes
			// the scan next update() if other devices are still waiting.
			NimBLEDevice::getScan()->stop();
			dev->beginGattConnection(advertisedDevice);
			return;
		}
	}
}

void BleManager::onScanEnd(const NimBLEScanResults &, int) {
	// Nothing to do — update() re-evaluates whether scanning is still
	// needed on the next loop() iteration.
}
