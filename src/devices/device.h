#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "../command.h"
#include "../display.h"

// Interface implemented by every controllable BLE peripheral driver.
//
// Several devices can be active at once (BleManager holds one BLE
// connection per active device). activate()/deactivate() just flip a
// want-connection flag — BleManager owns the single shared scan (NimBLE
// only scans one at a time) and calls beginGattConnection() once it spots
// a matching advertisement.
//
// The Menu fans an abstract Command out to every active device via
// handleCommand(); a device that doesn't support a given command just
// ignores it.
class Device {
public:
	virtual ~Device() = default;

	virtual const char *name() const = 0;
	virtual const char *advertisedName() const = 0;

	// True once the user toggled this device on in the device list.
	virtual bool isActive() const = 0;
	virtual bool isConnected() const = 0;
	virtual void activate() = 0;
	virtual void deactivate() = 0;

	// Called every loop iteration regardless of connection state.
	virtual void tick() = 0;

	// Translate an abstract command into this device's own protocol.
	virtual void handleCommand(Command cmd) = 0;

	// One status line ("Slider: 120/900"), drawn by the menu at the given
	// y — several active devices can be visible on screen at once.
	virtual void renderStatusLine(Adafruit_ST7789 &tft, int16_t y) = 0;

	// --- Used by BleManager only ---
	bool matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const {
		return adv->haveName() && adv->getName() == advertisedName();
	}
	virtual void beginGattConnection(const NimBLEAdvertisedDevice *adv) = 0;
};
