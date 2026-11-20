#pragma once

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "device.h"

// BLE HID peripheral presented to phones as a camera/headset remote. This
// side of the dual-role link is a GATT server: phones connect to *us* and
// receive Consumer Control reports. The server itself is shared with DVH
// Link and owned by BleServer (ble_server.h); a peer counts as a phone once
// its link is encrypted (HID requires it — the Mac's DVH Link never is).
//
// Multi-phone: the one PhoneDevice slot holds several bonded phones at once
// (up to kMaxPhones). A shutter notify fans out to every subscribed phone
// in a single call, so REC / STOP REC hit them all. HID-over-GATT to
// multiple hosts at once is not a well-trodden path — validate on the
// bench that both an iPhone and an Android hold the link and keep
// triggering.
class PhoneDevice : public Device {
public:
	static const int kMaxPhones = 3;

	const char *name() const override { return "Phone"; }
	const char *advertisedName() const override { return ""; }
	DeviceKind kind() const override { return DeviceKind::Camera; }
	const char *abbrev() const override { return "PH"; } // "ТЛ" in the design
	uint16_t identityColor565() const override;
	uint16_t identityTextColor565() const override;
	void drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t size, uint16_t color) const override;

	bool isActive() const override { return _active; }
	bool isConnected() const override { return _peerCount > 0; }
	void activate() override;
	void deactivate() override;

	void tick() override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_GFX &tft, int16_t y) override;

	bool usesCentralConnection() const override { return false; }
	void beginGattConnection(const NimBLEAdvertisedDevice *) override {}

	int cameraReady() const override { return _peerCount; }
	int cameraTotal() const override;

	// Live count of connected phones and stored bonds (for the UI).
	int connectedCount() const { return _peerCount; }
	int bondedPhoneCount() const { return NimBLEDevice::getNumBonds(); }

	// The Menu tells us when a take starts/stops so cameraTotal() can hold
	// the peak count (a phone dropping mid-take then reads as "1/2").
	void markTakeStart();
	void markTakeEnd();

	// --- BleServer hooks ---
	void setupHid(NimBLEServer *server); // once, before the server starts
	bool wantsAdvertising() const { return _active && _peerCount < kMaxPhones; }
	void onPhoneReady(uint16_t connHandle);       // link encrypted
	void onPeerDisconnected(uint16_t connHandle); // also: peer turned out to be the Mac

private:
	void sendShutterClick();
	void notifyPhones(uint16_t usage);

	NimBLEServer *_server = nullptr;
	NimBLEHIDDevice *_hid = nullptr;
	NimBLECharacteristic *_consumerInput = nullptr;
	bool _active = false;
	bool _releasePending = false;
	uint32_t _pressedAtMs = 0;
	uint32_t _lastBatteryUpdateMs = 0;

	uint16_t _peers[kMaxPhones] = {0}; // connection handles of connected phones
	int _peerCount = 0;

	bool _takeActive = false;
	int _takePeak = 0;
};

extern PhoneDevice phoneDevice;
