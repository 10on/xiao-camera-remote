#include "turntable_device.h"
#include "../theme.h"

TurntableDevice turntableDevice;

// UUIDs from ~/projects/ali-turn-table-upgrade/docs/ble-protocol.md
static const NimBLEUUID kServiceUUID("7ab1e001-0000-4000-8000-00000000cafe");
static const NimBLEUUID kCommandUUID("7ab1e002-0000-4000-8000-00000000cafe");
static const NimBLEUUID kSpeedUUID  ("7ab1e003-0000-4000-8000-00000000cafe");
static const NimBLEUUID kStatusUUID ("7ab1e004-0000-4000-8000-00000000cafe");

uint16_t TurntableDevice::identityColor565() const { return theme::kTurntableFill; }
uint16_t TurntableDevice::identityTextColor565() const { return theme::kTurntableText; }

// A disc with a centre spindle (rotating platter).
void TurntableDevice::drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t s, uint16_t c) const {
	int16_t r = s * 5 / 13;
	int16_t cx = x + s / 2, cy = y + s / 2;
	g.drawCircle(cx, cy, r, c);
	g.drawCircle(cx, cy, r - 3, c);
	g.fillCircle(cx, cy, 2, c);
}

const char *TurntableDevice::motionStateText() const {
	switch (_state) {
	case 1: return "RUNNING";
	case 2: return "OTA";
	case 3: return "LOW BATT";
	default: return "IDLE";
	}
}

// Match on the service UUID (in the primary adv packet) OR the name (which
// may only be in the scan response) — either alone is enough.
bool TurntableDevice::matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const {
	if (adv->haveServiceUUID() && adv->isAdvertisingService(kServiceUUID)) return true;
	return adv->haveName() && adv->getName() == "Turntable";
}

void TurntableDevice::activate() { _active = true; }

void TurntableDevice::deactivate() {
	_active = false;
	if (_client) _client->disconnect();
}

void TurntableDevice::beginGattConnection(const NimBLEAdvertisedDevice *adv) {
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
	_chStatus = svc->getCharacteristic(kStatusUUID);

	if (_chStatus && _chStatus->canNotify()) {
		_chStatus->subscribe(true, [this](NimBLERemoteCharacteristic *, uint8_t *data,
		                                   size_t len, bool) { onStatusNotify(data, len); });
	}
}

void TurntableDevice::onConnect(NimBLEClient *) { _connected = true; }

void TurntableDevice::onDisconnect(NimBLEClient *, int) {
	_connected = false;
	_state = 0;
	_flags = 0;
	_battPct = 0xFF;
	_chCommand = _chSpeed = _chStatus = nullptr;
	if (_client) {
		NimBLEDevice::deleteClient(_client);
		_client = nullptr;
	}
	// BleManager reconnects on the next update() while _active.
}

void TurntableDevice::sendCommand(char c) {
	if (_chCommand) _chCommand->writeValue(reinterpret_cast<uint8_t *>(&c), 1, false);
}

void TurntableDevice::sendSpeed(uint8_t idx) {
	_speedIdx = idx;
	if (_chSpeed) _chSpeed->writeValue(&idx, 1, false);
}

void TurntableDevice::onStatusNotify(const uint8_t *data, size_t len) {
	if (len < 6) return;
	_state      = data[0];
	_dirForward = data[1] == 0;
	_speedIdx   = data[2];
	_speedCount = data[3] ? data[3] : 4;
	_battPct    = data[4];
	_flags      = data[5];
}

void TurntableDevice::tick() {
	// Status arrives via notify; nothing to poll.
}

bool TurntableDevice::canExecuteCommand(Command cmd) const {
	if (!_connected) return false;
	if (cmd == Command::MoveForward || cmd == Command::MoveBackward ||
	    cmd == Command::StartProgram) {
		return _state != 3; // blocked only by a low-battery fault
	}
	return true;
}

void TurntableDevice::handleCommand(Command cmd) {
	if (!_connected) return;
	switch (cmd) {
	case Command::MoveForward:
		sendCommand('F');
		break;
	case Command::MoveBackward:
		sendCommand('B');
		break;
	case Command::StartProgram:
		sendCommand(_dirForward ? 'F' : 'B');
		break;
	case Command::StopMove:
	case Command::StopProgram:
	case Command::EmergencyStop:
		sendCommand('S');
		break;
	case Command::SpeedUp:
		if (_speedIdx + 1 < _speedCount) sendSpeed(_speedIdx + 1);
		break;
	case Command::SpeedDown:
		if (_speedIdx > 0) sendSpeed(_speedIdx - 1);
		break;
	case Command::ResetFault:
		sendCommand('R');
		break;
	default:
		break;
	}
}

void TurntableDevice::renderStatusLine(Adafruit_GFX &tft, int16_t y) {
	tft.setCursor(theme::kPadH, y);
	tft.setTextSize(theme::kSizeBody);
	tft.setTextColor(theme::kTextPrimary);

	if (!_connected) {
		tft.setTextColor(_active ? theme::kWarnFill : theme::kTextInactive);
		tft.printf("%s: %s", name(), _active ? "connecting..." : "off");
		return;
	}
	tft.printf("%s: %s %s S%u/%u", name(), motionStateText(),
	           _dirForward ? "fwd" : "rev", _speedIdx + 1, _speedCount);
	if (_battPct != 0xFF) tft.printf(" %u%%", _battPct);
}
