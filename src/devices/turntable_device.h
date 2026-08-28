#pragma once

#include <NimBLEDevice.h>
#include "device.h"

// BLE central driver for the "Turntable" peripheral (ali-turn-table-upgrade).
// Protocol reference: ~/projects/ali-turn-table-upgrade/docs/ble-protocol.md
// Same family as the slider: 1-byte Command char, Speed char (preset index),
// notifying 6-byte Status char. No homing, no endstops, no position.
class TurntableDevice : public Device, public NimBLEClientCallbacks {
public:
	const char *name() const override { return "Turntable"; }
	const char *advertisedName() const override { return "Turntable"; }
	DeviceKind kind() const override { return DeviceKind::Motion; }
	bool supportsHome() const override { return false; }
	int speedPercent() const override {
		uint8_t n = _speedCount ? _speedCount : 4;
		return (_speedIdx + 1) * 100 / n;
	}
	bool inFault() const override { return _state == 3; }
	const char *motionStateText() const override;
	const char *abbrev() const override { return "TT"; } // "СТ" in the design; custom font not built (see slider docs/screen-design.md)
	uint16_t identityColor565() const override;
	uint16_t identityTextColor565() const override;
	void drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t size, uint16_t color) const override;

	bool isActive() const override { return _active; }
	bool isConnected() const override { return _connected; }
	void activate() override;
	void deactivate() override;

	void tick() override;
	bool canExecuteCommand(Command cmd) const override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_GFX &tft, int16_t y) override;
	bool matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const override;
	void beginGattConnection(const NimBLEAdvertisedDevice *adv) override;

	// NimBLEClientCallbacks
	void onConnect(NimBLEClient *client) override;
	void onDisconnect(NimBLEClient *client, int reason) override;

private:
	void sendCommand(char c);
	void sendSpeed(uint8_t idx);
	void onStatusNotify(const uint8_t *data, size_t len);

	NimBLEClient *_client = nullptr;
	NimBLERemoteCharacteristic *_chCommand = nullptr;
	NimBLERemoteCharacteristic *_chSpeed = nullptr;
	NimBLERemoteCharacteristic *_chStatus = nullptr;

	bool _active = false;
	bool _connected = false;

	// From Status notify (byte layout in docs/ble-protocol.md).
	uint8_t _state = 0;       // 0 stopped, 1 running, 2 OTA, 3 fault/low-batt
	bool    _dirForward = true;
	uint8_t _speedIdx = 1;
	uint8_t _speedCount = 4;
	uint8_t _battPct = 0xFF;
	uint8_t _flags = 0;
};

extern TurntableDevice turntableDevice;
