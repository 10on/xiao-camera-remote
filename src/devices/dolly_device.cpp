#include "dolly_device.h"

#include "../theme.h"

DollyDevice dollyDevice;

namespace {

constexpr size_t kPacketSize = 9;
const NimBLEUUID kCommandServiceUuid("69400001-b5a3-f393-e0a9-e50e24dcca99");
const NimBLEUUID kWriteCharacteristicUuid("69400002-b5a3-f393-e0a9-e50e24dcca99");

// Literal packets recovered from the original client. The last byte is not
// a conventional checksum, so do not synthesize packets from field values.
const uint8_t kSpeedPackets[5][kPacketSize] = {
	{0x90, 0x01, 0x05, 0x01, 0x00, 0x01, 0x00, 0x00, 0x9A},
	{0x90, 0x01, 0x05, 0x01, 0x00, 0x02, 0x00, 0x00, 0x9A},
	{0x90, 0x01, 0x05, 0x01, 0x00, 0x03, 0x00, 0x00, 0x9A},
	{0x90, 0x01, 0x05, 0x01, 0x00, 0x04, 0x00, 0x00, 0x9A},
	{0x90, 0x01, 0x05, 0x01, 0x00, 0x05, 0x00, 0x00, 0x9A},
};
const uint8_t kMoveRight[kPacketSize] = {0x90, 0x01, 0x05, 0x01, 0x01, 0xFF, 0xFF, 0x00, 0x96};
const uint8_t kMoveLeft[kPacketSize] = {0x90, 0x01, 0x05, 0x01, 0x01, 0x00, 0xFF, 0x00, 0x96};
const uint8_t kStop[kPacketSize] = {0x90, 0x01, 0x05, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x97};
const uint8_t kManualMode[kPacketSize] = {0x90, 0x06, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x9C};
const uint8_t kLiveVideoMode[kPacketSize] = {0x90, 0x06, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x9D};
const uint8_t kConstantAccel[kPacketSize] = {0x90, 0x01, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x99};
const uint8_t kSlowAccel[kPacketSize] = {0x90, 0x01, 0x05, 0x01, 0x02, 0xFF, 0x00, 0x00, 0x98};
const uint8_t *const kEmergencyPackets[] = {
	kLiveVideoMode, kConstantAccel, kStop, kManualMode,
	kLiveVideoMode, kConstantAccel, kStop, kManualMode,
	kLiveVideoMode, kConstantAccel, kStop, kManualMode,
	kSlowAccel,
};
constexpr uint8_t kEmergencyPacketCount = sizeof(kEmergencyPackets) / sizeof(kEmergencyPackets[0]);

} // namespace

uint16_t DollyDevice::identityColor565() const {
	return theme::kDollyFill;
}

uint16_t DollyDevice::identityTextColor565() const {
	return theme::kDollyText;
}

bool DollyDevice::matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const {
	if (!adv->haveName()) return false;
	const std::string name = adv->getName();
	return name.compare(0, 9, "NEEWER-DL") == 0;
}

void DollyDevice::activate() {
	_active = true;
}

void DollyDevice::deactivate() {
	_active = false;
	if (_connected && _write) normalStop();
	if (_client) _client->disconnect();
}

void DollyDevice::beginGattConnection(const NimBLEAdvertisedDevice *adv) {
	_client = NimBLEDevice::createClient();
	_client->setClientCallbacks(this, false);

	if (!_client->connect(adv)) {
		NimBLEDevice::deleteClient(_client);
		_client = nullptr;
		return;
	}

	NimBLERemoteService *service = _client->getService(kCommandServiceUuid);
	if (!service) {
		_client->disconnect();
		return;
	}

	_write = service->getCharacteristic(kWriteCharacteristicUuid);
	if (!_write || !_write->canWriteNoResponse()) {
		_client->disconnect();
		return;
	}

	// The original client always performs SPEED5 -> ... -> SPEED1 before
	// accepting movement commands. Send the first step now; tick() spaces the
	// remaining writes by 100 ms without blocking the UI or other BLE links.
	_initializing = true;
	_nextInitSpeed = 4;
	writePacket(kSpeedPackets[4]);
	_nextInitAtMs = millis() + 100;
}

void DollyDevice::onConnect(NimBLEClient *) {
	_connected = true;
}

void DollyDevice::onDisconnect(NimBLEClient *, int) {
	_connected = false;
	_initializing = false;
	_nextInitSpeed = 0;
	_emergencyStep = 0;
	_write = nullptr;
	if (_client) {
		NimBLEDevice::deleteClient(_client);
		_client = nullptr;
	}
}

void DollyDevice::writePacket(const uint8_t *packet) {
	if (_write) _write->writeValue(packet, kPacketSize, false);
}

void DollyDevice::setSpeed(uint8_t level) {
	if (level < 1) level = 1;
	if (level > 5) level = 5;
	_speedLevel = level;
	writePacket(kSpeedPackets[level - 1]);
}

void DollyDevice::normalStop() {
	writePacket(kStop);
}

void DollyDevice::emergencyStop() {
	// Exact sequence from the reverse-engineered client's emergencystop().
	// Space write-without-response packets in tick() so the controller's BLE
	// queue cannot discard the tail of a 13-packet burst.
	_initializing = false;
	writePacket(kEmergencyPackets[0]);
	_emergencyStep = 1;
	_nextEmergencyAtMs = millis() + 20;
}

void DollyDevice::tick() {
	if (_emergencyStep && _connected && _write) {
		uint32_t now = millis();
		if (static_cast<int32_t>(now - _nextEmergencyAtMs) >= 0) {
			writePacket(kEmergencyPackets[_emergencyStep]);
			_emergencyStep++;
			if (_emergencyStep >= kEmergencyPacketCount) {
				_emergencyStep = 0;
			} else {
				_nextEmergencyAtMs = now + 20;
			}
		}
		return;
	}

	if (!_initializing || !_connected || !_write) return;
	uint32_t now = millis();
	if (static_cast<int32_t>(now - _nextInitAtMs) < 0) return;

	writePacket(kSpeedPackets[_nextInitSpeed - 1]);
	if (_nextInitSpeed == 1) {
		_speedLevel = 1;
		_initializing = false;
		return;
	}
	_nextInitSpeed--;
	_nextInitAtMs = now + 100;
}

void DollyDevice::handleCommand(Command cmd) {
	if (!_connected || !_write) return;
	if (cmd == Command::EmergencyStop) {
		emergencyStop();
		return;
	}
	if (_initializing || _emergencyStep) return;

	switch (cmd) {
	case Command::MoveForward:
		writePacket(kMoveRight);
		break;
	case Command::MoveBackward:
		writePacket(kMoveLeft);
		break;
	case Command::StopMove:
		normalStop();
		break;
	case Command::SpeedUp:
		setSpeed(_speedLevel + 1);
		break;
	case Command::SpeedDown:
		setSpeed(_speedLevel - 1);
		break;
	default:
		break;
	}
}

void DollyDevice::renderStatusLine(Adafruit_GFX &tft, int16_t y) {
	tft.setCursor(theme::kPadH, y);
	tft.setTextSize(theme::kSizeBody);
	tft.setTextColor(theme::kTextPrimary);
	if (!_connected) {
		tft.setTextColor(_active ? theme::kWarnFill : theme::kTextInactive);
		tft.printf("Dolly: %s", _active ? "linking..." : "off");
	} else if (_initializing) {
		tft.setTextColor(theme::kWarnFill);
		tft.print("Dolly: syncing...");
	} else {
		tft.printf("Dolly: ready S%u", _speedLevel);
	}
}
