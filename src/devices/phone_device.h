#pragma once

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "device.h"

// Phones as individual devices. Each PhoneDevice is a slot ("Phone 1".."Phone
// 4") bound to one bonded phone by its BLE identity address, persisted in
// NVS. A rig picks slots like any other device, so a configuration can use
// just the 13 mini over the table and leave the 16 Pro on the slider alone.
//
// All slots share one HID service on the remote's GATT server (BleServer):
// phones connect to *us*. A peer counts as a phone once its link is
// encrypted (HID requires it). Then PhoneHub binds it to its slot — a new
// phone takes the first free slot, preferring one the current rig wants.
// Every bonded phone stays linked whether or not its slot is in the running
// rig: the Record click goes only to the rig's slots. Dropping phones
// outside the rig made iOS give up reconnecting on its own.
// Side effect: a linked iPhone treats the remote as a keyboard (no
// on-screen keyboard while the remote is on).
//
// The Mac (DVH Link) may be bonded too (macOS pairs it as a HID keyboard):
// an unknown encrypted peer is decided only after kDecideDelayMs, giving the
// Mac time to subscribe to DVH Link, and the Mac's identity is remembered.
class PhoneDevice : public Device {
public:
	explicit PhoneDevice(int slot) : _slot(slot) {}

	const char *name() const override;
	const char *advertisedName() const override { return ""; }
	DeviceKind kind() const override { return DeviceKind::Camera; }
	const char *abbrev() const override { return "PH"; }
	uint16_t identityColor565() const override;
	uint16_t identityTextColor565() const override;
	void drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t size, uint16_t color) const override;

	bool isActive() const override { return _active; }
	bool isConnected() const override;
	void activate() override { _active = true; }
	void deactivate() override;

	void tick() override;
	void handleCommand(Command cmd) override;
	void renderStatusLine(Adafruit_GFX &tft, int16_t y) override;

	bool usesCentralConnection() const override { return false; }
	void beginGattConnection(const NimBLEAdvertisedDevice *) override {}

	int cameraReady() const override { return isConnected() ? 1 : 0; }
	int cameraTotal() const override { return _active ? 1 : 0; }

	// Device card -> Service: forget the phone bound to this slot.
	int serviceActionCount() const override { return 1; }
	const char *serviceActionLabel(int) const override { return "Forget phone"; }
	void runServiceAction(int) override;
	bool serviceInfo(int i, const char *&key, char *val, size_t valLen) const override;

	int slot() const { return _slot; }

private:
	int _slot;
	bool _active = false;
	bool _releasePending = false;
	uint32_t _pressedAtMs = 0;
};

namespace PhoneHub {
static const int kSlots = 4;
static const uint32_t kDecideDelayMs = 3000;

PhoneDevice *at(int slot);

void begin();                        // load slot bindings from NVS
void setupHid(NimBLEServer *server); // once, before the server starts
void tick();                         // each loop: decide pending peers, battery level
bool wantsAdvertising();             // an active slot is waiting for its phone

// BleServer / DvhLink hooks (NimBLE host task).
void onEncrypted(const NimBLEConnInfo &connInfo);
void onPeerDisconnected(uint16_t connHandle);
void onMacSubscribed(const NimBLEConnInfo &connInfo); // this peer is the Mac, not a phone

// PhoneDevice internals.
bool connected(int slot);
bool bound(int slot);
void dropSlot(int slot);   // disconnect the slot's phone (if connected)
void forgetSlot(int slot); // unbind + delete its bond
void notify(int slot, uint16_t usage);
bool addressOf(int slot, NimBLEAddress &out);
} // namespace PhoneHub

extern PhoneDevice phoneDevice;  // slot 0 — device index 1, "Phone 1"
extern PhoneDevice phoneDevice2; // slot 1
extern PhoneDevice phoneDevice3; // slot 2
extern PhoneDevice phoneDevice4; // slot 3
