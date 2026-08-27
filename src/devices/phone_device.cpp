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

uint16_t PhoneDevice::identityColor565() const {
	return theme::kPhoneFill;
}

uint16_t PhoneDevice::identityTextColor565() const {
	return theme::kPhoneText;
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
	// NimBLE (assert failed: ble_svc_gap_init, ble_svc_gap.c — resetGATT()
	// inside NimBLEServer::start() collides with the in-progress scan;
	// see h2zero/NimBLE-Arduino#557). BleManager almost always has a scan
	// running in the background whenever another device (e.g. the slider)
	// is active but not yet connected, which is exactly the state the
	// user is in when they first activate Phone. Stop it first —
	// BleManager::update() restarts it next tick if anything's still
	// waiting, so this doesn't strand a central-role connection attempt.
	NimBLEDevice::getScan()->stop();
	_server->start();

	NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
	advertising->setName("XIAO Camera Remote");
	advertising->setAppearance(HID_KEYBOARD);
	advertising->addServiceUUID(NimBLEUUID(static_cast<uint16_t>(0x1812)));
	advertising->enableScanResponse(true);
}

void PhoneDevice::startAdvertising() {
	if (!_active || !_server || _connected) return;
	NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
	if (!advertising->isAdvertising()) advertising->start();
}

void PhoneDevice::activate() {
	_active = true;
	ensureInitialized();
	startAdvertising();
}

void PhoneDevice::deactivate() {
	_active = false;
	_releasePending = false;
	NimBLEDevice::getAdvertising()->stop();
	if (_server) {
		for (uint16_t handle : _server->getPeerDevices()) {
			_server->disconnect(handle);
		}
	}
	_connected = false;
}

void PhoneDevice::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
	_connected = true;
	server->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 200);
}

void PhoneDevice::onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) {
	_connected = false;
	_releasePending = false;
	startAdvertising();
}

void PhoneDevice::tick() {
	if (_active && !_connected) startAdvertising();
	if (!_hid) return;

	uint32_t now = millis();
	if (_releasePending && now - _pressedAtMs >= 12) {
		uint16_t released = 0;
		_consumerInput->notify(reinterpret_cast<uint8_t *>(&released), sizeof(released));
		_releasePending = false;
	}
	if (now - _lastBatteryUpdateMs >= 30000) {
		_hid->setBatteryLevel(battery.percent(), _connected);
		_lastBatteryUpdateMs = now;
	}
}

void PhoneDevice::sendShutterClick() {
	if (!_connected || !_consumerInput) return;

	// A HID key is an edge, not a persistent state. The release is sent from
	// tick() after a short hold so the BLE stack cannot coalesce both reports.
	uint16_t usage = kConsumerVolumeIncrement;
	_consumerInput->notify(reinterpret_cast<uint8_t *>(&usage), sizeof(usage));
	_pressedAtMs = millis();
	_releasePending = true;
}

void PhoneDevice::handleCommand(Command cmd) {
	if (cmd == Command::Record || cmd == Command::StopRecord) {
		sendShutterClick();
	}
}

void PhoneDevice::renderStatusLine(Adafruit_GFX &tft, int16_t y) {
	tft.setCursor(theme::kPadH, y);
	tft.setTextSize(theme::kSizeBody);
	tft.setTextColor(theme::kTextPrimary);
	if (_connected) {
		tft.print("Phone: ready");
	} else {
		tft.setTextColor(_active ? theme::kWarnFill : theme::kTextInactive);
		tft.printf("Phone: %s", _active ? "pairing..." : "off");
	}
}
