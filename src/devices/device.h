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
// Broad category used by the Menu to decide a rig's layout: a Motion
// device can be a rig's Main (drives the control screen); a Camera device
// is always Secondary (the REC block) — see docs/design/ux-redesign.md §3.
enum class DeviceKind : uint8_t { Motion, Camera };

class Device {
public:
	virtual ~Device() = default;

	virtual const char *name() const = 0;
	virtual const char *advertisedName() const = 0;

	virtual DeviceKind kind() const = 0;

	// Whether Command::Home means anything for this device — the Main-centric
	// control screen only shows a Home affordance when it does (slider yes,
	// dolly no; see ux-redesign.md §7).
	virtual bool supportsHome() const { return false; }

	// Motion devices report speed as a 1..max level for the control screen's
	// numeral + segment bar (mock screen 4: "4 /8"). 0 = no speed concept.
	virtual int speedLevel() const { return 0; }
	virtual int speedLevelMax() const { return 0; }
	// Selected device-owned program shown on the control screen. nullptr means this
	// device only exposes direct/manual motion (see slider docs/11_program_api.md:
	// the pult drives the *program*, not motor direction).
	virtual const char *programName() const { return nullptr; }
	// Confirmed program run-state from the device's own notify — never a local guess.
	virtual bool programRunning() const { return false; }
	// Device is in an error/fault state that blocks starting motion.
	virtual bool inFault() const { return false; }
	// Short state word for the control-screen status pill, from real telemetry
	// ("IDLE"/"RUNNING"/"MANUAL"/"HOMING"/"ERROR"...). nullptr -> the Menu falls
	// back to its own locally-tracked guess (devices with no state feedback).
	virtual const char *motionStateText() const { return nullptr; }
	// Endstop telemetry for the two ends, shown as dots (no local logic).
	virtual bool endstop1() const { return false; }
	virtual bool endstop2() const { return false; }

	// Camera devices: how many recorders this one slot currently represents
	// (a phone driver can hold several simultaneously). ready <= total; total
	// stays at the take's peak while recording so a mid-take drop shows as
	// "1/2". Non-camera devices leave these at 0.
	virtual int cameraReady() const { return 0; }
	virtual int cameraTotal() const { return 0; }

	// Identity for the device-list row: a 2-letter abbreviation (Latin for
	// now — see docs/screen-design.md's "Cyrillic text" note, the design's
	// actual Cyrillic copy needs a custom font that isn't built yet) and
	// its RGB565 identity fill/text colors, per the design's device
	// identity axis (hue = which device, never mixed with state color).
	virtual const char *abbrev() const = 0;
	virtual uint16_t identityColor565() const = 0;
	virtual uint16_t identityTextColor565() const = 0;

	// Draw this device's pictogram (mock's icon set — rails, dolly body,
	// phone outline) into the `size`×`size` chip at (x,y), in `color`.
	// Default falls back to the 2-letter abbrev centred, so a device
	// without an override still renders.
	virtual void drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t size,
	                       uint16_t color) const {
		g.setFont(nullptr);
		g.setTextSize(1);
		g.setTextColor(color);
		int16_t bx, by;
		uint16_t bw, bh;
		g.getTextBounds(abbrev(), 0, 0, &bx, &by, &bw, &bh);
		g.setCursor(x + (size - (int16_t)bw) / 2, y + (size - (int16_t)bh) / 2);
		g.print(abbrev());
	}

	// True once the user toggled this device on in the device list.
	virtual bool isActive() const = 0;
	virtual bool isConnected() const = 0;
	virtual void activate() = 0;
	virtual void deactivate() = 0;

	// Called every loop iteration regardless of connection state.
	virtual void tick() = 0;

	// Translate an abstract command into this device's own protocol.
	// The control screen uses this preflight hook before changing its local UI state.
	// Device firmware remains authoritative; this only prevents commands that current
	// telemetry already proves cannot be accepted (for example, driving into an endstop).
	virtual bool canExecuteCommand(Command cmd) const {
		(void)cmd;
		return isConnected();
	}
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
