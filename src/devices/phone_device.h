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
	const char *abbrev() const override { return "PH"; } // "ТЛ" in the design
	uint16_t identityColor565() const override;
	uint16_t identityTextColor565() const override;

	bool isActive() const override { return _active; }
	bool isConnected() const override { return _connected; }
	void activate() override;
	void deactivate() override;

	void tick() override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_GFX &tft, int16_t y) override;

	bool usesCentralConnection() const override { return false; }
	void beginGattConnection(const NimBLEAdvertisedDevice *) override {}

	// NimBLEServerCallbacks
	void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
	void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;

private:
	void ensureInitialized();
	void startAdvertising();
	void sendShutterClick();

	NimBLEServer *_server = nullptr;
	NimBLEHIDDevice *_hid = nullptr;
	NimBLECharacteristic *_consumerInput = nullptr;
	bool _active = false;
	bool _connected = false;
	bool _releasePending = false;
	uint32_t _pressedAtMs = 0;
	uint32_t _lastBatteryUpdateMs = 0;
};

extern PhoneDevice phoneDevice;
