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

	// The remote is discoverable for the Mac while a configuration is
	// running and the Mac isn't connected yet. Once connected it stays
	// connected across configurations.
	void setSessionActive(bool on) { _session = on; }
	bool wantsAdvertising() const { return _session && _mac == kNone; }

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
	bool _session = false;
	bool _inTake = false;
};

extern DvhLink dvhLink;
