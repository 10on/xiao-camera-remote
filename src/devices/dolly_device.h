#pragma once

#include <NimBLEDevice.h>
#include "device.h"

// BLE central driver for the Neewer DL200 camera dolly.
// Uses the literal packets from the reverse-engineered Neewer DL200 protocol;
// its trailing byte is not a conventional checksum and must not be generated.
class DollyDevice : public Device, public NimBLEClientCallbacks {
public:
	const char *name() const override { return "Dolly"; }
	const char *advertisedName() const override { return "NEEWER-DL"; }
	const char *abbrev() const override { return "DL"; } // "ТЖ" in the design
	uint16_t identityColor565() const override;
	uint16_t identityTextColor565() const override;

	bool isActive() const override { return _active; }
	bool isConnected() const override { return _connected; }
	void activate() override;
	void deactivate() override;

	void tick() override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_GFX &tft, int16_t y) override;

	bool matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const override;
	void beginGattConnection(const NimBLEAdvertisedDevice *adv) override;

	// NimBLEClientCallbacks
	void onConnect(NimBLEClient *client) override;
	void onDisconnect(NimBLEClient *client, int reason) override;

private:
	void writePacket(const uint8_t *packet);
	void setSpeed(uint8_t level);
	void normalStop();
	void emergencyStop();

	NimBLEClient *_client = nullptr;
	NimBLERemoteCharacteristic *_write = nullptr;
	bool _active = false;
	bool _connected = false;
	bool _initializing = false;
	uint8_t _nextInitSpeed = 0;
	uint8_t _speedLevel = 1;
	uint32_t _nextInitAtMs = 0;
	uint8_t _emergencyStep = 0;
	uint32_t _nextEmergencyAtMs = 0;
};

extern DollyDevice dollyDevice;
