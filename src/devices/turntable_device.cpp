#include "turntable_device.h"
#include "../theme.h"

// "СТ" in the design; custom font not built (see slider docs/screen-design.md)
TurntableDevice turntableDevice({"Table SM", "Turntable", "SM",
                                 "7ab1e001-0000-4000-8000-00000000cafe",
                                 theme::kTurntableFill, theme::kTurntableText, false});
// Protocol: ~/projects/mid_table/docs/ble-protocol.md
TurntableDevice midTableDevice({"Table MID", "MidTable", "MID",
                                "7ab1e101-0000-4000-8000-00000000cafe",
                                theme::kMidTableFill, theme::kMidTableText, true});

// Characteristic UUIDs are shared by every table; the service UUID differs.
// From ~/projects/ali-turn-table-upgrade/docs/ble-protocol.md
static const NimBLEUUID kCommandUUID("7ab1e002-0000-4000-8000-00000000cafe");
static const NimBLEUUID kSpeedUUID  ("7ab1e003-0000-4000-8000-00000000cafe");
static const NimBLEUUID kStatusUUID ("7ab1e004-0000-4000-8000-00000000cafe");

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
	if (adv->haveServiceUUID() && adv->isAdvertisingService(_serviceUUID)) return true;
	return adv->haveName() && adv->getName() == _profile.advertisedName;
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

	NimBLERemoteService *svc = _client->getService(_serviceUUID);
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

void TurntableDevice::onConnect(NimBLEClient *) {
	_connected = true;
}

void TurntableDevice::onDisconnect(NimBLEClient *, int) {
	_connected = false;
	_state = 0;
	_flags = 0;
	_battPct = 0xFF;
	memset(_ip, 0, sizeof(_ip));
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
	if (len >= 10) memcpy(_ip, data + 6, 4);
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
	tft.printf("%s: %s %s %d%%", name(), motionStateText(),
	           _dirForward ? "fwd" : "rev", speedPercent());
	if (_battPct != 0xFF) tft.printf(" %u%%", _battPct);
}

// --- Service screen ---------------------------------------------------------
// Command bytes: ~/projects/mid_table/docs/ble-protocol.md

int TurntableDevice::serviceActionCount() const { return _profile.service ? 4 : 0; }

const char *TurntableDevice::serviceActionLabel(int i) const {
	switch (i) {
	case 0: return _state == 2 ? "WiFi OTA: stop" : "WiFi OTA: start";
	case 1: return "Re-init driver";
	case 2: return "Buzzer test";
	case 3: return "Clear battery cutoff";
	default: return "";
	}
}

void TurntableDevice::runServiceAction(int i) {
	if (!_connected) return;
	switch (i) {
	case 0: sendCommand(_state == 2 ? 'o' : 'O'); break;
	case 1: sendCommand('T'); break;
	case 2: sendCommand('Z'); break;
	case 3: sendCommand('R'); break;
	default: break;
	}
}

bool TurntableDevice::serviceInfo(int i, const char *&key, char *val, size_t n) const {
	if (!_profile.service) return false;
	switch (i) {
	case 0:
		key = "BATTERY";
		if (!_connected || _battPct == 0xFF) snprintf(val, n, "-");
		else snprintf(val, n, "%u%%%s", _battPct, (_flags & 0x02) ? " CUTOFF" : (_flags & 0x01) ? " LOW" : "");
		return true;
	case 1:
		key = "DRIVER";
		snprintf(val, n, "%s", !_connected ? "-" : (_flags & 0x04) ? "OK" : "NO UART");
		return true;
	case 2:
		key = "WIFI OTA";
		if (!_connected || _state != 2) snprintf(val, n, "off");
		else if (!(_flags & 0x08)) snprintf(val, n, "connecting...");
		else snprintf(val, n, "%u.%u.%u.%u", _ip[0], _ip[1], _ip[2], _ip[3]);
		return true;
	default:
		return false;
	}
}
