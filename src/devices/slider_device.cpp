#include "slider_device.h"

SliderDevice sliderDevice;

// UUIDs from ~/projects/slider/docs/03_ble_protocol.md
static const NimBLEUUID kServiceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static const NimBLEUUID kCommandUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
static const NimBLEUUID kSpeedUUID("d8de624e-140f-4a22-8594-e2216b84a5f2");
static const NimBLEUUID kPositionUUID("a1b2c3d4-e5f6-4789-a012-3456789abcde");
static const NimBLEUUID kCurrentUUID("f1e2d3c4-b5a6-4978-9012-3456789abcde");
static const NimBLEUUID kStatusUUID("1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e");

void SliderDevice::activate() {
	_active = true;
}

void SliderDevice::deactivate() {
	_active = false;
	if (_client) {
		_client->disconnect();
	}
}

void SliderDevice::beginGattConnection(const NimBLEAdvertisedDevice *adv) {
	_client = NimBLEDevice::createClient();
	_client->setClientCallbacks(this, false);

	if (!_client->connect(adv)) {
		NimBLEDevice::deleteClient(_client);
		_client = nullptr;
		return;
	}

	NimBLERemoteService *svc = _client->getService(kServiceUUID);
	if (!svc) {
		_client->disconnect();
		return;
	}

	_chCommand = svc->getCharacteristic(kCommandUUID);
	_chSpeed = svc->getCharacteristic(kSpeedUUID);
	_chPosition = svc->getCharacteristic(kPositionUUID);
	_chCurrent = svc->getCharacteristic(kCurrentUUID);
	_chStatus = svc->getCharacteristic(kStatusUUID);

	if (_chStatus && _chStatus->canNotify()) {
		_chStatus->subscribe(true, [this](NimBLERemoteCharacteristic *,
		                                   uint8_t *data, size_t len, bool) {
			onStatusNotify(data, len);
		});
	}

	sendSpeed(_speedUs);
}

void SliderDevice::onConnect(NimBLEClient *) {
	_connected = true;
}

void SliderDevice::onDisconnect(NimBLEClient *, int) {
	_connected = false;
	_chCommand = _chSpeed = _chPosition = _chCurrent = _chStatus = nullptr;
	if (_client) {
		NimBLEDevice::deleteClient(_client);
		_client = nullptr;
	}
	// If still active, BleManager's ensureScanning() picks this device
	// back up as "waiting" on the next update() and reconnects.
}

void SliderDevice::sendCommand(char cmd) {
	if (_chCommand) {
		_chCommand->writeValue(reinterpret_cast<uint8_t *>(&cmd), 1, false);
	}
}

void SliderDevice::sendSpeed(uint16_t usPerStep) {
	usPerStep = constrain(usPerStep, (uint16_t)100, (uint16_t)5000);
	_speedUs = usPerStep;
	if (_chSpeed) {
		_chSpeed->writeValue(reinterpret_cast<uint8_t *>(&_speedUs), 2, false);
	}
}

void SliderDevice::onStatusNotify(const uint8_t *data, size_t len) {
	if (len < 9) return;
	memcpy(&_position, data + 0, 4);
	_flags = data[4];
	memcpy(&_travelDistance, data + 5, 4);
}

void SliderDevice::tick() {
	// Status arrives via notify; nothing to poll here.
}

void SliderDevice::handleCommand(Command cmd) {
	if (!_connected) return;

	switch (cmd) {
	case Command::MoveForward:
		sendCommand('F');
		break;
	case Command::MoveBackward:
		sendCommand('B');
		break;
	case Command::StopMove:
		sendCommand('S');
		break;
	case Command::Home:
		sendCommand('H');
		break;
	case Command::SpeedUp:
		sendSpeed(_speedUs > 200 ? _speedUs - 200 : 100);
		break;
	case Command::SpeedDown:
		sendSpeed(_speedUs < 4800 ? _speedUs + 200 : 5000);
		break;
	default:
		break; // Record/StopRecord don't apply to the slider
	}
}

void SliderDevice::renderStatusLine(Adafruit_ST7789 &tft, int16_t y) {
	tft.setCursor(10, y);
	tft.setTextSize(2);
	tft.setTextColor(0xFFFF);

	if (!_connected) {
		tft.printf("%s: %s", name(), _active ? "connecting..." : "off");
		return;
	}
	tft.printf("%s: %ld/%ld  %uus", name(), static_cast<long>(_position),
	           static_cast<long>(_travelDistance), _speedUs);
}
