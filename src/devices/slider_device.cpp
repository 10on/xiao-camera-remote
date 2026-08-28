#include "slider_device.h"

#include <math.h>

#include "../theme.h"

SliderDevice sliderDevice;

uint16_t SliderDevice::identityColor565() const {
	return theme::kSliderFill;
}

uint16_t SliderDevice::identityTextColor565() const {
	return theme::kSliderText;
}

// Rail with two posts + a carriage block on top (mock icon set).
void SliderDevice::drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t s, uint16_t c) const {
	int16_t iw = s * 15 / 26, ih = s * 12 / 26;
	int16_t ix = x + (s - iw) / 2, iy = y + (s - ih) / 2;
	int16_t rail = iy + ih * 6 / 12;
	g.fillRect(ix, rail, iw, 2, c);                    // rail
	g.fillRect(ix, iy + ih / 5, 2, ih * 3 / 5, c);      // left post
	g.fillRect(ix + iw - 2, iy + ih / 5, 2, ih * 3 / 5, c); // right post
	g.fillRoundRect(ix + iw / 4, iy, iw / 2, ih / 2, 1, c); // carriage
}

// UUIDs from ~/projects/slider/docs/03_ble_protocol.md
static const NimBLEUUID kServiceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static const NimBLEUUID kCommandUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
static const NimBLEUUID kSpeedUUID("d8de624e-140f-4a22-8594-e2216b84a5f2");
static const NimBLEUUID kPositionUUID("a1b2c3d4-e5f6-4789-a012-3456789abcde");
static const NimBLEUUID kCurrentUUID("f1e2d3c4-b5a6-4978-9012-3456789abcde");
static const NimBLEUUID kStatusUUID("1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e");
static const NimBLEUUID kProgramUUID("c0f1c001-0000-4000-8000-00000000c0de");

enum : uint8_t {
	kProgramPingPong = 1,
	kProgramSelect = 0,
	kProgramStart = 1,
	kProgramStop = 2,
	kProgramConfigure = 3,
};

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
	_chProgram = svc->getCharacteristic(kProgramUUID);

	if (_chStatus && _chStatus->canNotify()) {
		_chStatus->subscribe(true, [this](NimBLERemoteCharacteristic *,
		                                   uint8_t *data, size_t len, bool) {
			onStatusNotify(data, len);
		});
	}
	if (_chProgram && _chProgram->canNotify()) {
		_chProgram->subscribe(true, [this](NimBLERemoteCharacteristic *,
		                                    uint8_t *data, size_t len, bool) {
			onProgramNotify(data, len);
		});
	}

	// Select the slider-owned default without overwriting its saved parameters.
	// Program notify fills _speedPercent with the slider's real cfg.speed.
	if (_chProgram) sendProgram(kProgramSelect);
	else sendSpeed(_speedPercent); // compatibility with older slider firmware
}

void SliderDevice::onConnect(NimBLEClient *) {
	_connected = true;
}

void SliderDevice::onDisconnect(NimBLEClient *, int) {
	_connected = false;
	_flags = 0;
	_state = 0;
	_error = 0;
	_program = kProgramPingPong;
	_programCaps = 0;
	_programRunning = false;
	_chCommand = _chSpeed = _chPosition = _chCurrent = _chStatus = nullptr;
	_chProgram = nullptr;
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

// Legacy Speed characteristic (firmware without the Program API): 2-byte uint16
// step interval in us.  Convert percent -> interval with the same geometric curve
// the slider uses internally (config.cpp speedToInterval): every +1% multiplies
// step frequency by a constant ratio, so the dial stays perceptually linear.
void SliderDevice::sendSpeed(uint8_t percent) {
	percent = constrain(percent, (uint8_t)1, (uint8_t)100);
	float t = (percent - 1) / 99.0f;
	uint16_t interval = (uint16_t)lroundf(5000.0f * powf(100.0f / 5000.0f, t));
	if (_chSpeed) {
		_chSpeed->writeValue(reinterpret_cast<uint8_t *>(&interval), 2, false);
	}
}

void SliderDevice::sendProgram(uint8_t action, uint8_t speedPercent,
                               uint8_t startPoint, uint8_t flags) {
	if (!_chProgram) return;
	uint8_t packet[6] = {1, kProgramPingPong, action, speedPercent, startPoint, flags};
	_chProgram->writeValue(packet, sizeof(packet), false);
}

// One arrow press = one 10% step on the slider's native 1..100% scale.  We snap
// to the 10% grid first so a press is always predictable even when the slider is
// sitting on an odd value set from its own menu.  The slider's geometric curve
// turns each 10% into a ~1.5x speed change — coarse but even end to end.
void SliderDevice::nudgeSpeed(int delta) {
	int p = _speedPercent;
	if (delta > 0)      p = (p / 10) * 10 + 10;
	else if (delta < 0) p = (p - 1) / 10 * 10;
	_speedPercent = (uint8_t)constrain(p, 10, 100);

	// Trust our own value over the slider's echo until the CONFIGURE round-trips;
	// the slider notifies cfg.speed at 10Hz and the stale value would bounce the UI.
	_speedEchoMuteUntilMs = millis() + 600;

	if (_chProgram) sendProgram(kProgramConfigure, _speedPercent);
	else sendSpeed(_speedPercent);
}

int SliderDevice::speedPercent() const {
	return _speedPercent;
}

const char *SliderDevice::motionStateText() const {
	switch (_state) {
	case 0: return "IDLE";
	case 1: return "MANUAL";
	case 2: return "GOTO";
	case 3: return "HOMING";
	case 4: return "PARKING";
	case 5: return "ERROR";
	case 6: return "SLEEP";
	case 7: return _programRunning ? "RUNNING" : "IDLE";
	default: return "IDLE";
	}
}

void SliderDevice::onStatusNotify(const uint8_t *data, size_t len) {
	if (len < 9) return;
	memcpy(&_position, data + 0, 4);
	_flags = data[4];
	memcpy(&_travelDistance, data + 5, 4);
	// Status v2 appends state and error while keeping the first 9 bytes compatible with v1.
	_state = len >= 10 ? data[9] : ((_flags & 0x20) ? 1 : 0);
	_error = len >= 11 ? data[10] : 0;
}

void SliderDevice::onProgramNotify(const uint8_t *data, size_t len) {
	if (len < 6 || data[0] != 1) return;
	_program = data[1];
	_programRunning = data[2] != 0;
	// data[3] is the slider's live cfg.speed (1..100%).  Adopt it unless a local
	// nudge is still in flight — see nudgeSpeed().
	if ((int32_t)(millis() - _speedEchoMuteUntilMs) >= 0)
		_speedPercent = constrain(data[3], (uint8_t)1, (uint8_t)100);
	_programCaps = data[5];
	if (len >= 7) _state = data[6];
	if (len >= 8) _error = data[7];
}

void SliderDevice::tick() {
	// Status arrives via notify; nothing to poll here.
}

bool SliderDevice::canExecuteCommand(Command cmd) const {
	if (!_connected) return false;

	if (cmd == Command::MoveForward || cmd == Command::MoveBackward) {
		const bool blockedByEndstop = cmd == Command::MoveForward
			? (_flags & 0x02) != 0
			: (_flags & 0x01) != 0;
		if (blockedByEndstop) return false;

		// F/B are accepted only in IDLE or MANUAL.  In MANUAL the opposite command is a
		// direction change; all automated modes remain owned by the slider.
		return _state == 0 || _state == 1;
	}
	if (cmd == Command::StartProgram) {
		return _chProgram && (_programCaps & 0x03) == 0x03 && _state == 0;
	}

	return true;
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
	case Command::EmergencyStop:
		sendCommand('S');
		break;
	case Command::Home:
		sendCommand('H');
		break;
	case Command::SpeedUp:
		nudgeSpeed(+1);
		break;
	case Command::SpeedDown:
		nudgeSpeed(-1);
		break;
	case Command::StartProgram:
		if (_chProgram) sendProgram(kProgramStart);
		break;
	case Command::StopProgram:
		if (_chProgram) sendProgram(kProgramStop);
		else sendCommand('S');
		break;
	case Command::ResetFault:
		sendCommand('R');
		break;
	default:
		break; // Record/StopRecord don't apply to the slider
	}
}

void SliderDevice::renderStatusLine(Adafruit_GFX &tft, int16_t y) {
	tft.setCursor(theme::kPadH, y);
	tft.setTextSize(theme::kSizeBody);
	tft.setTextColor(theme::kTextPrimary);

	if (!_connected) {
		tft.setTextColor(_active ? theme::kWarnFill : theme::kTextInactive);
		tft.printf("%s: %s", name(), _active ? "connecting..." : "off");
		return;
	}
	tft.printf("%s: %s %s %u%%%s%s", name(), programName(),
	           _programRunning ? "RUN" : "IDLE",
	           _speedPercent,
	           (_flags & 0x01) ? " E1" : "", (_flags & 0x02) ? " E2" : "");
}
