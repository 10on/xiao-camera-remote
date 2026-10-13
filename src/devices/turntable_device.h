#pragma once

#include <NimBLEDevice.h>
#include "device.h"

// BLE central driver for turntable peripherals sharing one protocol:
// 1-byte Command char, Speed char (preset index), notifying 6-byte Status
// char. No homing, no endstops, no position. Protocol reference:
// ~/projects/ali-turn-table-upgrade/docs/ble-protocol.md
//
// Each physical table is its own registered device: same characteristics,
// different service UUID (that's what tells them apart in a scan).
struct TurntableProfile {
	const char *name;
	const char *advertisedName;
	const char *abbrev;
	const char *serviceUuid;
	uint16_t fill565;
	uint16_t text565;
	bool service; // firmware takes the service Command bytes (O/o/T/Z) + 10-byte Status
};

class TurntableDevice : public Device, public NimBLEClientCallbacks {
public:
	explicit TurntableDevice(const TurntableProfile &profile)
		: _profile(profile), _serviceUUID(profile.serviceUuid) {}

	const char *name() const override { return _profile.name; }
	const char *advertisedName() const override { return _profile.advertisedName; }
	DeviceKind kind() const override { return DeviceKind::Motion; }
	bool supportsHome() const override { return false; }
	int speedPercent() const override {
		uint8_t n = _speedCount ? _speedCount : 4;
		return (_speedIdx + 1) * 100 / n;
	}
	bool inFault() const override { return _state == 3; }
	const char *motionStateText() const override;
	const char *abbrev() const override { return _profile.abbrev; }
	uint16_t identityColor565() const override { return _profile.fill565; }
	uint16_t identityTextColor565() const override { return _profile.text565; }
	void drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t size, uint16_t color) const override;

	bool isActive() const override { return _active; }
	bool isConnected() const override { return _connected; }
	void activate() override;
	void deactivate() override;

	void tick() override;
	bool canExecuteCommand(Command cmd) const override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_GFX &tft, int16_t y) override;
	int serviceActionCount() const override;
	const char *serviceActionLabel(int i) const override;
	void runServiceAction(int i) override;
	bool serviceInfo(int i, const char *&key, char *val, size_t valLen) const override;
	bool matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const override;
	void beginGattConnection(const NimBLEAdvertisedDevice *adv) override;

	// NimBLEClientCallbacks
	void onConnect(NimBLEClient *client) override;
	void onDisconnect(NimBLEClient *client, int reason) override;

private:
	void sendCommand(char c);
	void sendSpeed(uint8_t idx);
	void onStatusNotify(const uint8_t *data, size_t len);

	const TurntableProfile _profile;
	const NimBLEUUID _serviceUUID;

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
	uint8_t _flags = 0;       // bit0 low, bit1 cutoff, bit2 driver ok, bit3 OTA ready
	uint8_t _ip[4] = {0, 0, 0, 0}; // while OTA ready (service firmware only)
};

extern TurntableDevice turntableDevice; // ali-turn-table-upgrade
extern TurntableDevice midTableDevice;  // ~/projects/mid_table
