#pragma once

#include <NimBLEDevice.h>

// DVH Link — optional GATT service for the Mac running dvh
// (davinchi-helper, docs/remote-ble.md). The remote only *reports* takes;
// it shows nothing and never waits on the Mac, so a missing Mac changes
// nothing about recording.
//
//   Event   (NOTIFY, remote -> Mac): ASCII word — hello / take / stop
//   Display (WRITE,  Mac -> remote): accepted and ignored for now
//
// No encryption: the Mac connects without pairing. A connection that
// subscribes to Event is the Mac and never counts as a phone.
class DvhLink : public NimBLECharacteristicCallbacks {
public:
	static constexpr const char *kServiceUuid = "d7b10001-6e0d-4c1e-8f3a-5d1a7e000000";
	static constexpr const char *kEventUuid = "d7b10002-6e0d-4c1e-8f3a-5d1a7e000000";
	static constexpr const char *kDisplayUuid = "d7b10003-6e0d-4c1e-8f3a-5d1a7e000000";

	void setup(NimBLEServer *server); // from BleServer::begin(), before start()
	void tick();                      // sends a queued "hello"

	// hello goes out this long after the Event subscription: a Mac bonded
	// with the remote (macOS pairs it as a HID keyboard) gets its CCCD
	// restored on reconnect, before the app itself subscribes — an
	// immediate hello would be dropped, and the app's own subscribe raises
	// no new event because the CCCD value doesn't change.
	static const uint32_t kHelloDelayMs = 1500;

	// The remote is discoverable for the Mac whenever the Mac isn't
	// connected — from boot, so it's linked before the first take (a take
	// right after launching a configuration would otherwise be lost while
	// the Mac is still connecting). Side effect: bonded phones may
	// reconnect outside a phone configuration too; they get no clicks.
	bool wantsAdvertising() const { return _mac == kNone; }

	bool macConnected() const { return _mac != kNone; }
	bool isMac(uint16_t connHandle) const { return _mac != kNone && _mac == connHandle; }

	// Take start/end. stop() is a no-op unless a take was started, so every
	// path that ends a take (STOP, E-Stop, Main lost, leaving the
	// configuration) can call it unconditionally.
	void take();
	void stop();

	void onPeerDisconnected(uint16_t connHandle);

	// NimBLECharacteristicCallbacks
	void onSubscribe(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo, uint16_t subValue) override;
	void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo) override;

private:
	static constexpr uint16_t kNone = 0xFFFF;

	void send(const char *word);

	NimBLECharacteristic *_event = nullptr;
	NimBLECharacteristic *_display = nullptr;
	volatile uint16_t _mac = kNone; // set from the NimBLE host task
	volatile bool _helloPending = false;
	volatile uint32_t _helloAtMs = 0;
	bool _inTake = false;
};

extern DvhLink dvhLink;
