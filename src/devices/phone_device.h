#pragma once

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "device.h"

// BLE HID peripheral presented to a phone as a camera/headset remote.
// Unlike the motion-device drivers, this side of the dual-role link is a
// server: the phone connects to us and receives Consumer Control reports.
class PhoneDevice : public Device, public NimBLEServerCallbacks {
public:
	const char *name() const override { return "Phone"; }
	const char *advertisedName() const override { return ""; }
	DeviceKind kind() const override { return DeviceKind::Camera; }
	const char *abbrev() const override { return "PH"; } // "ТЛ" in the design
	uint16_t identityColor565() const override;
	uint16_t identityTextColor565() const override;
	void drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t size, uint16_t color) const override;

	bool isActive() const override { return _active; }
	bool isConnected() const override { return _connected; }
	void activate() override;
	void deactivate() override;

	void tick() override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_GFX &tft, int16_t y) override;

	bool usesCentralConnection() const override { return false; }
	void beginGattConnection(const NimBLEAdvertisedDevice *) override {}

	// --- Multiple bonded phones, one at a time (ux: "quick switch") ---
	// The remote is a single-peer HID peripheral; it keeps bonds with
	// several phones and connects whichever it's told to prefer. Switching
	// is a disconnect + re-advertise — the target phone (already bonded,
	// nearby) reconnects on its own, ~2-5s. Not simultaneous dual-camera.
	int bondedPhoneCount() const;
	// "Phone 1" / "Phone 2" for the currently-linked phone, or "-".
	const char *activePhoneLabel() const;
	// Prefer the next bonded phone and bounce the current one. No-op with
	// <2 bonds. Returns true if a switch was actually kicked off.
	bool switchToNextPhone();

	// NimBLEServerCallbacks
	void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
	void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;

private:
	void ensureInitialized();
	void startAdvertising();
	void sendShutterClick();
	int bondIndexOf(const NimBLEAddress &addr) const; // -1 if not a bond

	NimBLEServer *_server = nullptr;
	NimBLEHIDDevice *_hid = nullptr;
	NimBLECharacteristic *_consumerInput = nullptr;
	bool _active = false;
	bool _connected = false;
	bool _releasePending = false;
	uint32_t _pressedAtMs = 0;
	uint32_t _lastBatteryUpdateMs = 0;

	NimBLEAddress _connectedAddr{}; // identity address of the linked phone
	NimBLEAddress _preferredAddr{};
	bool _havePreference = false;
	uint32_t _switchDeadlineMs = 0; // after this, accept any bonded phone again
};

extern PhoneDevice phoneDevice;
