#pragma once

#include <NimBLEDevice.h>
#include "device.h"

// BLE client driver for the "Camera_Slider" peripheral.
// Protocol reference: ~/projects/slider/docs/03_ble_protocol.md
class SliderDevice : public Device, public NimBLEClientCallbacks {
public:
	const char *name() const override { return "Slider"; }
	const char *advertisedName() const override { return "Camera_Slider"; }
	DeviceKind kind() const override { return DeviceKind::Motion; }
	bool supportsHome() const override { return true; }
	int speedLevel() const override;
	int speedLevelMax() const override { return 8; }
	const char *programName() const override { return _chProgram ? "PING PONG" : nullptr; }
	bool programRunning() const override { return _programRunning; }
	bool inFault() const override { return _error != 0 || _state == 5; }
	const char *motionStateText() const override;
	bool endstop1() const override { return (_flags & 0x01) != 0; }
	bool endstop2() const override { return (_flags & 0x02) != 0; }
	const char *abbrev() const override { return "SL"; } // "СЛ" in the design; see docs/screen-design.md
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
	void beginGattConnection(const NimBLEAdvertisedDevice *adv) override;

	// NimBLEClientCallbacks
	void onConnect(NimBLEClient *client) override;
	void onDisconnect(NimBLEClient *client, int reason) override;

private:
	void sendCommand(char cmd);
	void sendSpeed(uint16_t usPerStep);
	void sendProgram(uint8_t action, uint8_t speedPercent = 0, uint8_t startPoint = 0xFF,
	                 uint8_t flags = 0);
	void setSpeedLevel(uint8_t level);
	void onStatusNotify(const uint8_t *data, size_t len);
	void onProgramNotify(const uint8_t *data, size_t len);

	NimBLEClient *_client = nullptr;
	NimBLERemoteCharacteristic *_chCommand = nullptr;
	NimBLERemoteCharacteristic *_chSpeed = nullptr;
	NimBLERemoteCharacteristic *_chPosition = nullptr;
	NimBLERemoteCharacteristic *_chCurrent = nullptr;
	NimBLERemoteCharacteristic *_chStatus = nullptr;
	NimBLERemoteCharacteristic *_chProgram = nullptr;

	bool _active = false;    // user toggled this device on
	bool _connected = false;

	// Last known status (from notify)
	int32_t _position = 0;
	int32_t _travelDistance = 0;
	uint8_t _flags = 0;
	uint8_t _state = 0;
	uint8_t _error = 0;
	uint8_t _program = 1; // Ping-Pong is the normal/default program
	uint8_t _programCaps = 0;
	bool _programRunning = false;

	uint8_t _speedLevel = 4;
	uint16_t _speedUs = 2900; // level 4/8 mapped onto the protocol's 5000..100us range
};

extern SliderDevice sliderDevice;
