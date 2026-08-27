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

	// Identity for the device-list row: a 2-letter abbreviation (Latin for
	// now — see docs/screen-design.md's "Cyrillic text" note, the design's
	// actual Cyrillic copy needs a custom font that isn't built yet) and
	// its RGB565 identity fill/text colors, per the design's device
	// identity axis (hue = which device, never mixed with state color).
	virtual const char *abbrev() const = 0;
	virtual uint16_t identityColor565() const = 0;
	virtual uint16_t identityTextColor565() const = 0;

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
	virtual void renderStatusLine(Adafruit_GFX &tft, int16_t y) = 0;

	// --- Used by BleManager only ---
	// Most drivers are BLE-central clients discovered by scanning. Local
	// peripheral drivers (for example the phone HID driver) override this so
	// BleManager still ticks them, but never waits/scans for an advertisement.
	virtual bool usesCentralConnection() const { return true; }
	virtual bool matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const {
		return usesCentralConnection() && adv->haveName() && adv->getName() == advertisedName();
	}
	virtual void beginGattConnection(const NimBLEAdvertisedDevice *adv) = 0;
};
