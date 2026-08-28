#include "ble_manager.h"

#include <Arduino.h>

// Set -DBLE_SCAN_DEBUG=1 in platformio.ini build_flags to log every
// advertisement seen and every driver match over serial.
#ifndef BLE_SCAN_DEBUG
#define BLE_SCAN_DEBUG 0
#endif

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
		if (_devices[i]->usesCentralConnection() && _devices[i]->isActive() &&
		    !_devices[i]->isConnected()) return true;
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

	// Handle a match onResult() queued last tick — see ble_manager.h for
	// why this can't happen synchronously inside onResult() itself.
	if (_pendingConnectDevice) {
		Device *dev = _pendingConnectDevice;
		const NimBLEAdvertisedDevice *adv = _pendingConnectAdvertised;
		_pendingConnectDevice = nullptr;
		_pendingConnectAdvertised = nullptr;
		dev->beginGattConnection(adv);
	}

	ensureScanning();
}

void BleManager::onResult(const NimBLEAdvertisedDevice *advertisedDevice) {
	if (_pendingConnectDevice) return; // already have one queued for this tick

#if BLE_SCAN_DEBUG
	Serial.printf("[scan] %s  name='%s'  svc=%d\n",
	              advertisedDevice->getAddress().toString().c_str(),
	              advertisedDevice->haveName() ? advertisedDevice->getName().c_str() : "?",
	              advertisedDevice->haveServiceUUID() ? 1 : 0);
#endif

	for (size_t i = 0; i < _deviceCount; i++) {
		Device *dev = _devices[i];
		if (dev->usesCentralConnection() && dev->isActive() && !dev->isConnected() &&
		    dev->matchesAdvertisement(advertisedDevice)) {
#if BLE_SCAN_DEBUG
			Serial.printf("[scan] -> match %s\n", dev->name());
#endif
			// Stop scanning now (cheap, doesn't touch GAP connection
			// state) but defer the actual connect() to update() — see
			// ble_manager.h's comment on _pendingConnectDevice.
			NimBLEDevice::getScan()->stop();
			_pendingConnectDevice = dev;
			_pendingConnectAdvertised = advertisedDevice;
			return;
		}
	}
}

void BleManager::onScanEnd(const NimBLEScanResults &, int) {
	// Nothing to do — update() re-evaluates whether scanning is still
	// needed on the next loop() iteration.
}
