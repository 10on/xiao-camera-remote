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
// Advertising is shared too: the remote advertises while either the phone
// driver wants another phone or DVH Link wants the Mac (tick()).
class BleServer : public NimBLEServerCallbacks {
public:
	void begin(); // once, from setup(), after NimBLEDevice::init()
	void tick();  // each loop: start/stop advertising on demand

	NimBLEServer *server() const { return _server; }

	void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
	void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;
	void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;

private:
	NimBLEServer *_server = nullptr;
};

extern BleServer bleServer;
