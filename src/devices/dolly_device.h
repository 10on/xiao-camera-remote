#pragma once

#include <NimBLEDevice.h>
#include "device.h"

// BLE central driver for the Neewer DL200 camera dolly.
// Packets + sequences from the reference web client
// (everlastengineering.com ble-dl200.min.js): a move is always
// ACCEL -> SPEEDn -> ENABLEMOTIONxxx, a stop is LIVEVIDEO -> MANUAL -> STOP,
// every write in a sequence spaced ~90-100 ms. A bare speed packet has no
// effect — speed only applies as part of a move sequence.
class DollyDevice : public Device, public NimBLEClientCallbacks {
public:
	const char *name() const override { return "Dolly"; }
	const char *advertisedName() const override { return "NEEWER-DL"; }
	DeviceKind kind() const override { return DeviceKind::Motion; }
	int speedPercent() const override { return _speedLevel * 20; } // 5 packets -> 20..100%
	const char *abbrev() const override { return "DL"; } // "ТЖ" in the design
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

	bool matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const override;
	void beginGattConnection(const NimBLEAdvertisedDevice *adv) override;

	// NimBLEClientCallbacks
	void onConnect(NimBLEClient *client) override;
	void onDisconnect(NimBLEClient *client, int reason) override;

private:
	void writePacket(const uint8_t *packet);
	void emergencyStop();
	void queueMove(); // ACCEL + SPEEDn + ENABLEMOTION at the current direction/speed
	void queueStop(); // LIVEVIDEO + MANUAL + STOP
	void enqueue(const uint8_t *packet);
	void clearQueue();

	NimBLEClient *_client = nullptr;
	NimBLERemoteCharacteristic *_write = nullptr;
	bool _active = false;
	bool _connected = false;
	bool _initializing = false;
	uint8_t _nextInitSpeed = 0;
	uint32_t _nextInitAtMs = 0;
	uint8_t _emergencyStep = 0;
	uint32_t _nextEmergencyAtMs = 0;

	uint8_t _speedLevel = 3; // 1..5
	bool _dirRight = true;
	bool _moving = false;

	// Speed changes while moving are debounced: the level updates instantly
	// (for the UI) but the ACCEL+SPEED+MOVE apply-sequence only fires once
	// the user stops pressing.
	bool _speedDirty = false;
	uint32_t _speedApplyAtMs = 0;

	// Paced write-without-response queue: the DL200 silently drops packets
	// sent faster than a connection interval.
	static const int kTxQueue = 12;
	const uint8_t *_tx[kTxQueue] = {nullptr};
	int _txHead = 0;
	int _txCount = 0;
	uint32_t _lastTxMs = 0;
};

extern DollyDevice dollyDevice;
