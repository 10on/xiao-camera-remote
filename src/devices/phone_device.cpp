#include "phone_device.h"

#include "../battery.h"
#include "../theme.h"

PhoneDevice phoneDevice;

namespace {

constexpr uint8_t kReportId = 1;
constexpr uint16_t kConsumerVolumeIncrement = 0x00E9;

// One 16-bit Consumer Control usage per report. Volume Increment is what
// commodity BLE camera shutters send; native iOS/Android camera apps map it
// to the photo shutter or video start/stop action.
uint8_t kConsumerReportMap[] = {
	0x05, 0x0C,       // Usage Page (Consumer)
	0x09, 0x01,       // Usage (Consumer Control)
	0xA1, 0x01,       // Collection (Application)
	0x85, kReportId,  //   Report ID
	0x15, 0x00,       //   Logical Minimum (0)
	0x26, 0xFF, 0x03, //   Logical Maximum (1023)
	0x19, 0x00,       //   Usage Minimum (0)
	0x2A, 0xFF, 0x03, //   Usage Maximum (1023)
	0x75, 0x10,       //   Report Size (16)
	0x95, 0x01,       //   Report Count (1)
	0x81, 0x00,       //   Input (Data, Array, Absolute)
	0xC0,             // End Collection
};

} // namespace

uint16_t PhoneDevice::identityColor565() const { return theme::kPhoneFill; }
uint16_t PhoneDevice::identityTextColor565() const { return theme::kPhoneText; }

// Phone body outline + speaker slot (mock icon set).
void PhoneDevice::drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t s, uint16_t c) const {
	int16_t iw = s * 10 / 26, ih = s * 15 / 26;
	if (iw < 8) iw = 8;
	int16_t ix = x + (s - iw) / 2, iy = y + (s - ih) / 2;
	g.drawRoundRect(ix, iy, iw, ih, 2, c);
	g.drawRoundRect(ix + 1, iy + 1, iw - 2, ih - 2, 2, c); // ~2px stroke
	g.fillRect(ix + iw / 2 - 1, iy + 3, 3, 1, c);          // speaker
}

void PhoneDevice::ensureInitialized() {
	if (_server) return;

	// HID report characteristics require an encrypted link. No-input/output
	// pairing gives the phone a standard "pair" confirmation and keeps the
	// bond in NimBLE storage for automatic reconnection after reboot.
	NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
	NimBLEDevice::setSecurityAuth(true, false, true);

	_server = NimBLEDevice::createServer();
	_server->setCallbacks(this, false);
	_server->advertiseOnDisconnect(false);

	_hid = new NimBLEHIDDevice(_server);
	_hid->setManufacturer("XIAO Remote");
	_hid->setPnp(0x02, 0x303A, 0x4001, 0x0100); // USB-IF source, Espressif VID
	_hid->setHidInfo(0x00, 0x02);                // normally-connectable HID
	_hid->setReportMap(kConsumerReportMap, sizeof(kConsumerReportMap));
	_consumerInput = _hid->getInputReport(kReportId);
	_hid->setBatteryLevel(battery.percent());

	// Starting a GATT server while a central-role scan is active crashes
	// NimBLE (assert failed: ble_svc_gap_init — resetGATT() inside
	// NimBLEServer::start() collides with the in-progress scan; see
	// h2zero/NimBLE-Arduino#557). Stop it first — BleManager restarts it
	// next tick if anything's still waiting.
	NimBLEDevice::getScan()->stop();
	_server->start();

	NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
	advertising->setName("XIAO Camera Remote");
	advertising->setAppearance(HID_KEYBOARD);
	advertising->addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));
	advertising->enableScanResponse(true);
}

// Advertise whenever we're active and have room for another phone; stop
// once full so we're not discoverable for no reason.
void PhoneDevice::updateAdvertising() {
	if (!_server) return;
	NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
	bool want = _active && _peerCount < kMaxPhones;
	if (want && !adv->isAdvertising()) adv->start();
	else if (!want && adv->isAdvertising()) adv->stop();
}

void PhoneDevice::activate() {
	_active = true;
	ensureInitialized();
	updateAdvertising();
}

void PhoneDevice::deactivate() {
	_active = false;
	_releasePending = false;
	_takeActive = false;
	if (_server) {
		NimBLEDevice::getAdvertising()->stop();
		for (uint16_t h : _server->getPeerDevices()) _server->disconnect(h);
	}
	_peerCount = 0;
}

int PhoneDevice::cameraTotal() const {
	if (_takeActive) return _takePeak;
	return _peerCount > 0 ? _peerCount : (_active ? 1 : 0);
}

void PhoneDevice::markTakeStart() {
	_takeActive = true;
	_takePeak = _peerCount > 0 ? _peerCount : 1;
}

void PhoneDevice::markTakeEnd() { _takeActive = false; }

void PhoneDevice::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
	uint16_t h = connInfo.getConnHandle();
	// Full already (shouldn't happen — we stop advertising) or a duplicate.
	for (int i = 0; i < _peerCount; i++)
		if (_peers[i] == h) return;
	if (_peerCount >= kMaxPhones) {
		server->disconnect(h);
		return;
	}
	_peers[_peerCount++] = h;
	server->updateConnParams(h, 12, 24, 0, 200);
	updateAdvertising();
}

void PhoneDevice::onDisconnect(NimBLEServer *, NimBLEConnInfo &connInfo, int) {
	uint16_t h = connInfo.getConnHandle();
	for (int i = 0; i < _peerCount; i++) {
		if (_peers[i] != h) continue;
		for (int j = i; j < _peerCount - 1; j++) _peers[j] = _peers[j + 1];
		_peerCount--;
		break;
	}
	_releasePending = false;
	updateAdvertising();
}

void PhoneDevice::tick() {
	if (_active) updateAdvertising();
	if (!_hid) return;

	uint32_t now = millis();
	if (_releasePending && now - _pressedAtMs >= 12) {
		uint16_t released = 0;
		_consumerInput->notify(reinterpret_cast<uint8_t *>(&released), sizeof(released));
		_releasePending = false;
	}
	if (now - _lastBatteryUpdateMs >= 30000) {
		_hid->setBatteryLevel(battery.percent(), _peerCount > 0);
		_lastBatteryUpdateMs = now;
	}
}

void PhoneDevice::sendShutterClick() {
	if (_peerCount == 0 || !_consumerInput) return;

	// A HID key is an edge, not a persistent state. notify() with no handle
	// fans the report to every subscribed phone at once. The release is
	// sent from tick() after a short hold so the stack can't coalesce them.
	uint16_t usage = kConsumerVolumeIncrement;
	_consumerInput->notify(reinterpret_cast<uint8_t *>(&usage), sizeof(usage));
	_pressedAtMs = millis();
	_releasePending = true;
}

void PhoneDevice::handleCommand(Command cmd) {
	if (cmd == Command::Record || cmd == Command::StopRecord) sendShutterClick();
}

void PhoneDevice::renderStatusLine(Adafruit_GFX &tft, int16_t y) {
	tft.setCursor(theme::kPadH, y);
	tft.setTextSize(theme::kSizeBody);
	tft.setTextColor(theme::kTextPrimary);
	if (_peerCount > 0) {
		tft.printf("Phone: %d ready", _peerCount);
	} else {
		tft.setTextColor(_active ? theme::kWarnFill : theme::kTextInactive);
		tft.printf("Phone: %s", _active ? "pairing..." : "off");
	}
}
