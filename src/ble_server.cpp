#include "ble_server.h"

#include "dvh_link.h"
#include "devices/phone_device.h"

BleServer bleServer;

void BleServer::begin() {
	if (_server) return;

	// HID report characteristics require an encrypted link. No-input/output
	// pairing gives the phone a standard "pair" confirmation and keeps the
	// bond in NimBLE storage for automatic reconnection after reboot. DVH
	// Link characteristics have no security flags, so the Mac never pairs.
	NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
	NimBLEDevice::setSecurityAuth(true, false, true);

	_server = NimBLEDevice::createServer();
	_server->setCallbacks(this, false);
	_server->advertiseOnDisconnect(false); // tick() decides

	PhoneHub::setupHid(_server);
	dvhLink.setup(_server);

	NimBLEDevice::getScan()->stop(); // belt and braces — see #557 above
	_server->start();

	// Primary packet: flags(3) + appearance(4) + HID 0x1812(4) + DVH Link
	// 128-bit UUID(18) = 29 of 31 bytes. The name goes to the scan response
	// (enableScanResponse() must come before setName() for that).
	NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
	adv->enableScanResponse(true);
	adv->setName("XIAO Camera Remote");
	adv->setAppearance(HID_KEYBOARD);
	adv->addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));
	adv->addServiceUUID(NimBLEUUID(DvhLink::kServiceUuid));
}

void BleServer::tick() {
	if (!_server) return;
	NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
	bool want = PhoneHub::wantsAdvertising() || dvhLink.wantsAdvertising();
	if (want && !adv->isAdvertising()) {
		bool ok = adv->start();
		static uint32_t lastFailLogMs = 0;
		if (!ok && millis() - lastFailLogMs > 5000) {
			lastFailLogMs = millis();
			Serial.printf("[adv] start FAILED (%u peers)\n", (unsigned)_server->getConnectedCount());
		}
	} else if (!want && adv->isAdvertising()) {
		adv->stop();
	}

	// State log on change (diagnostics: "phone can't see the remote").
	static int lastSig = -1;
	int sig = (want ? 1 : 0) | (adv->isAdvertising() ? 2 : 0) | (_server->getConnectedCount() << 2);
	bool scanning = NimBLEDevice::getScan()->isScanning();
	sig |= scanning ? 1 << 8 : 0;
	if (sig != lastSig) {
		lastSig = sig;
		Serial.printf("[adv] want=%d on=%d peers=%u mac=%d scan=%d\n", want, adv->isAdvertising(),
		              (unsigned)_server->getConnectedCount(), dvhLink.macConnected(), scanning);
	}
	// Bond list every 30 s (diagnostics: "keys keep getting lost").
	static uint32_t lastBondsMs = 0;
	if (millis() - lastBondsMs > 30000) {
		lastBondsMs = millis();
		int n = NimBLEDevice::getNumBonds();
		Serial.printf("[adv] own %s type %u power %d dBm\n", NimBLEDevice::getAddress().toString().c_str(),
		              (unsigned)NimBLEDevice::getAddress().getType(), NimBLEDevice::getPower());
		Serial.printf("[adv] on=%d peers=%u scan=%d\n", adv->isAdvertising(),
		              (unsigned)_server->getConnectedCount(), scanning);
		for (int i = 0; i < PhoneHub::kSlots; i++) {
			NimBLEAddress a;
			bool b = PhoneHub::addressOf(i, a);
			Serial.printf("[slot] Phone %d: %s%s%s\n", i + 1, b ? a.toString().c_str() : "-",
			              PhoneHub::connected(i) ? " linked" : "",
			              PhoneHub::at(i)->isActive() ? " in-rig" : "");
		}
		Serial.printf("[bonds] %d:", n);
		for (int i = 0; i < n; i++) Serial.printf(" %s", NimBLEDevice::getBondedAddress(i).toString().c_str());
		Serial.println();
	}
}

void BleServer::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
	// Who it is (phone or Mac) is only known later: a phone encrypts the
	// link (HID needs it), the Mac subscribes to DVH Link Event.
	// Supervision timeout 6 s (units of 10 ms): the radio is shared with the
	// scan and the central links, a 2 s timeout dropped the Mac too often.
	server->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 600);
	Serial.printf("[srv] connect conn %u from %s\n", connInfo.getConnHandle(),
	              connInfo.getAddress().toString().c_str());
}

void BleServer::onDisconnect(NimBLEServer *, NimBLEConnInfo &connInfo, int reason) {
	uint16_t h = connInfo.getConnHandle();
	Serial.printf("[srv] disconnect conn %u reason 0x%x\n", h, reason);
	PhoneHub::onPeerDisconnected(h);
	dvhLink.onPeerDisconnected(h);
}

void BleServer::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
	if (!connInfo.isSlave() || !connInfo.isEncrypted()) return; // ours as peripheral only
	PhoneHub::onEncrypted(connInfo);
}
