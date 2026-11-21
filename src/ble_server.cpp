#include "ble_server.h"

#include <string.h>

#include "dvh_link.h"
#include "devices/phone_device.h"

BleServer bleServer;

static bool g_setupOk[4]; // adv set data/scan rsp results, logged every 30 s

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
	// No auto-restart on disconnect with extended advertising: tick() decides.

	PhoneHub::setupHid(_server);
	dvhLink.setup(_server);

	NimBLEDevice::getScan()->stop(); // belt and braces — see #557 above
	_server->start();

	// Two advertising sets (BLE 5 extended advertising, legacy PDUs so any
	// phone sees them):
	//   kPhoneInst — public address, HID keyboard: phones pair and bond.
	//   kMacInst   — own random static address, DVH Link only: the Mac's
	//                bridge connects without pairing. Advertised as a keyboard
	//                macOS paired the remote as HID and forced HID link
	//                parameters (2 s supervision timeout) that the busy radio
	//                missed.
	NimBLEExtAdvertising *adv = NimBLEDevice::getAdvertising();

	NimBLEExtAdvertisement phone;
	phone.setLegacyAdvertising(true);
	phone.setConnectable(true);
	phone.setScannable(true);
	phone.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	phone.setAppearance(HID_KEYBOARD);
	phone.addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));
	NimBLEExtAdvertisement phoneRsp;
	phoneRsp.setName("XIAO Camera Remote");
	g_setupOk[0] = adv->setInstanceData(kPhoneInst, phone);
	g_setupOk[1] = adv->setScanResponseData(kPhoneInst, phoneRsp);

	NimBLEExtAdvertisement mac;
	mac.setLegacyAdvertising(true);
	mac.setConnectable(true);
	mac.setScannable(true);
	mac.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	mac.addServiceUUID(NimBLEUUID(DvhLink::kServiceUuid));
	mac.setAddress(macAddress());
	NimBLEExtAdvertisement macRsp;
	macRsp.setName("XIAO Remote DVH");
	g_setupOk[2] = adv->setInstanceData(kMacInst, mac);
	g_setupOk[3] = adv->setScanResponseData(kMacInst, macRsp);
}

// Random static address of the Mac set: the public one with the two top
// bits set (static random) and the low byte flipped — stable across boots,
// so the bridge sees the same peripheral.
NimBLEAddress BleServer::macAddress() {
	uint64_t v = NimBLEDevice::getAddress(); // public, 48 bits
	v ^= 0x5A;                              // differ in the low byte
	v |= 0xC00000000000ULL;                 // static random: top two bits 11
	return NimBLEAddress(v, BLE_ADDR_RANDOM);
}

// Hook in NimBLE's Security Manager (scripts/patch_nimble.py), called under
// the host lock on a Pairing Request: refuse it on the Mac set (our random
// address). DVH Link needs no encryption; without a bond macOS doesn't treat
// the remote as a keyboard and keeps the link parameters we ask for.
extern "C" int ble_sm_app_allow_pairing(uint16_t, const ble_addr_t *ourAddr) {
	return ourAddr->type != BLE_ADDR_RANDOM;
}

bool BleServer::viaMacSet(uint16_t connHandle) {
	ble_gap_conn_desc d;
	if (ble_gap_conn_find(connHandle, &d) != 0) return false;
	return d.our_ota_addr.type == BLE_ADDR_RANDOM;
}

void BleServer::tick() {
	if (!_server) return;
	NimBLEExtAdvertising *adv = NimBLEDevice::getAdvertising();
	bool wantPhone = PhoneHub::wantsAdvertising();
	bool wantMac = dvhLink.wantsAdvertising();
	bool want = wantPhone || wantMac;
	static uint32_t lastFailLogMs = 0;
	for (uint8_t inst = 0; inst < 2; inst++) {
		bool w = inst == kPhoneInst ? wantPhone : wantMac;
		bool on = adv->isActive(inst);
		if (w && !on) {
			if (!adv->start(inst) && millis() - lastFailLogMs > 5000) {
				lastFailLogMs = millis();
				Serial.printf("[adv] start %u FAILED (%u peers)\n", inst, (unsigned)_server->getConnectedCount());
			}
		} else if (!w && on) {
			adv->stop(inst);
		}
	}

	// State log on change (diagnostics: "phone can't see the remote").
	static int lastSig = -1;
	int sig = (wantPhone ? 1 : 0) | (wantMac ? 2 : 0) | (adv->isActive(kPhoneInst) ? 1 << 9 : 0) |
	          (adv->isActive(kMacInst) ? 1 << 10 : 0) | (_server->getConnectedCount() << 2);
	bool scanning = NimBLEDevice::getScan()->isScanning();
	sig |= scanning ? 1 << 8 : 0;
	if (sig != lastSig) {
		lastSig = sig;
		Serial.printf("[adv] phone want=%d on=%d, mac want=%d on=%d, peers=%u mac=%d scan=%d\n", wantPhone,
		              adv->isActive(kPhoneInst), wantMac, adv->isActive(kMacInst),
		              (unsigned)_server->getConnectedCount(), dvhLink.macConnected(), scanning);
	}
	// Bond list every 30 s (diagnostics: "keys keep getting lost").
	static uint32_t lastBondsMs = 0;
	if (millis() - lastBondsMs > 30000) {
		lastBondsMs = millis();
		int n = NimBLEDevice::getNumBonds();
		Serial.printf("[adv] own %s, mac set %s, power %d dBm\n", NimBLEDevice::getAddress().toString().c_str(),
		              macAddress().toString().c_str(), NimBLEDevice::getPower());
		Serial.printf("[adv] setup phone %d/%d mac %d/%d\n", g_setupOk[0], g_setupOk[1], g_setupOk[2], g_setupOk[3]);
		Serial.printf("[adv] phone on=%d mac on=%d peers=%u scan=%d\n", adv->isActive(kPhoneInst), adv->isActive(kMacInst),
		              (unsigned)_server->getConnectedCount(), scanning);
		for (int i = 0; i < PhoneHub::kSlots; i++) {
			NimBLEAddress a;
			bool b = PhoneHub::addressOf(i, a);
			Serial.printf("[slot] Phone %d: %s%s%s\n", i + 1, b ? a.toString().c_str() : "-",
			              PhoneHub::connected(i) ? " linked" : "",
			              PhoneHub::at(i)->isActive() ? " in-rig" : "");
		}
		for (uint16_t h : _server->getPeerDevices()) {
			NimBLEConnInfo ci = _server->getPeerInfoByHandle(h);
			Serial.printf("[peer] conn %u %s id %s via %s enc=%d bond=%d itvl=%u lat=%u to=%u0ms%s\n", h,
			              ci.getAddress().toString().c_str(), ci.getIdAddress().toString().c_str(),
			              viaMacSet(h) ? "mac" : "phone", ci.isEncrypted(), ci.isBonded(), ci.getConnInterval(),
			              ci.getConnLatency(), ci.getConnTimeout(), dvhLink.isMac(h) ? " DVH" : "");
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
	Serial.printf("[srv] connect conn %u from %s via %s set\n", connInfo.getConnHandle(),
	              connInfo.getAddress().toString().c_str(), viaMacSet(connInfo.getConnHandle()) ? "mac" : "phone");
}

// How many times we asked each connection for a 6 s supervision timeout
// after the central changed the parameters: at most kMaxAsks, no ping-pong.
static const int kMaxAsks = 3;
static uint16_t g_askH[8];
static uint8_t g_askN[8];
static uint16_t g_askLastTo[8]; // NimBLE reports one update more than once
static int g_askCount = 0;

static int askSlot(uint16_t h) {
	for (int i = 0; i < g_askCount; i++)
		if (g_askH[i] == h) return i;
	if (g_askCount == 8) return -1;
	g_askH[g_askCount] = h;
	g_askN[g_askCount] = 0;
	g_askLastTo[g_askCount] = 0;
	return g_askCount++;
}

static void forgetAsked(uint16_t h) {
	for (int i = 0; i < g_askCount; i++)
		if (g_askH[i] == h) {
			g_askCount--;
			g_askH[i] = g_askH[g_askCount];
			g_askN[i] = g_askN[g_askCount];
			g_askLastTo[i] = g_askLastTo[g_askCount];
			return;
		}
}

void BleServer::onConnParamsUpdate(NimBLEConnInfo &connInfo) {
	uint16_t h = connInfo.getConnHandle();
	uint16_t itvl = connInfo.getConnInterval(), lat = connInfo.getConnLatency(), to = connInfo.getConnTimeout();
	Serial.printf("[srv] params conn %u itvl %u lat %u timeout %u0 ms\n", h, itvl, lat, to);
	if (!connInfo.isSlave()) return;
	if (to >= 600) {
		int i = askSlot(h);
		if (i >= 0) g_askLastTo[i] = to;
		return;
	}
	int i = askSlot(h);
	if (i < 0 || g_askN[i] >= kMaxAsks || g_askLastTo[i] == to) return;
	g_askLastTo[i] = to;
	uint8_t *n = &g_askN[i];
	(*n)++;
	// macOS sets 15 ms, latency 22, 2 s timeout. Keep its interval and
	// latency, ask for 6 s (Apple's max).
	_server->updateConnParams(h, itvl, itvl, lat, 600);
	Serial.printf("[srv] ask conn %u timeout 6 s (#%u)\n", h, *n);
}

void BleServer::onDisconnect(NimBLEServer *, NimBLEConnInfo &connInfo, int reason) {
	uint16_t h = connInfo.getConnHandle();
	forgetAsked(h);
	Serial.printf("[srv] disconnect conn %u reason 0x%x\n", h, reason);
	PhoneHub::onPeerDisconnected(h);
	dvhLink.onPeerDisconnected(h);
}

void BleServer::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
	if (!connInfo.isSlave() || !connInfo.isEncrypted()) return; // ours as peripheral only
	if (viaMacSet(connInfo.getConnHandle())) {
		// DVH Link needs no pairing; the Mac paired anyway — not a phone.
		Serial.printf("[srv] conn %u paired via the mac set\n", connInfo.getConnHandle());
		return;
	}
	PhoneHub::onEncrypted(connInfo);
}
