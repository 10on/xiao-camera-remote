#pragma once

#include <NimBLEDevice.h>
#include "device.h"

// BLE client driver for the "Camera_Slider" peripheral.
// Protocol reference: ~/projects/slider/docs/03_ble_protocol.md
class SliderDevice : public Device, public NimBLEClientCallbacks {
public:
	const char *name() const override { return "Slider"; }
	const char *advertisedName() const override { return "Camera_Slider"; }

	bool isActive() const override { return _active; }
	bool isConnected() const override { return _connected; }
	void activate() override;
	void deactivate() override;

	void tick() override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_ST7789 &tft, int16_t y) override;
	void beginGattConnection(const NimBLEAdvertisedDevice *adv) override;

	// NimBLEClientCallbacks
	void onConnect(NimBLEClient *client) override;
	void onDisconnect(NimBLEClient *client, int reason) override;

private:
	void sendCommand(char cmd);
	void sendSpeed(uint16_t usPerStep);
	void onStatusNotify(const uint8_t *data, size_t len);

	NimBLEClient *_client = nullptr;
	NimBLERemoteCharacteristic *_chCommand = nullptr;
	NimBLERemoteCharacteristic *_chSpeed = nullptr;
	NimBLERemoteCharacteristic *_chPosition = nullptr;
	NimBLERemoteCharacteristic *_chCurrent = nullptr;
	NimBLERemoteCharacteristic *_chStatus = nullptr;

	bool _active = false;    // user toggled this device on
	bool _connected = false;

	// Last known status (from notify)
	int32_t _position = 0;
	int32_t _travelDistance = 0;
	uint8_t _flags = 0;

	uint16_t _speedUs = 1500; // mid-range default, matches protocol's 100..5000 range
};

extern SliderDevice sliderDevice;
