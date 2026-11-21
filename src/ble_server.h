#pragma once

#include <NimBLEDevice.h>

// Owns the single GATT server (the peripheral side of the dual-role link).
// It hosts two services on one server:
//   - HID (PhoneDevice) — phones connect and receive the shutter report;
//   - DVH Link (DvhLink) — the Mac running dvh learns about takes.
//
// Created once in setup(), before BleManager ever scans: starting a GATT
// server while a central-role scan is active crashes NimBLE (assert in
// ble_svc_gap_init — resetGATT() inside NimBLEServer::start() collides with
// the in-progress scan; see h2zero/NimBLE-Arduino#557). Services can't be
// added after start() without the same resetGATT(), so both are set up here.
//
// Two advertising sets (tick()): the phone set (public address, HID) while
// a rig's phone is missing, the Mac set (random address, DVH Link only, no
// pairing) while the Mac is not linked.
class BleServer : public NimBLEServerCallbacks {
public:
	void begin(); // once, from setup(), after NimBLEDevice::init()
	void tick();  // each loop: start/stop advertising on demand

	NimBLEServer *server() const { return _server; }

	void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
	void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;
	void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;
	void onConnParamsUpdate(NimBLEConnInfo &connInfo) override;

	static NimBLEAddress macAddress(); // the Mac set's random static address
	static bool viaMacSet(uint16_t connHandle);

private:
	static const uint8_t kPhoneInst = 0;
	static const uint8_t kMacInst = 1;
	NimBLEServer *_server = nullptr;
};

extern BleServer bleServer;
