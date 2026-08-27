#include "dolly_device.h"

#include "../theme.h"

DollyDevice dollyDevice;

namespace {

constexpr size_t kPacketSize = 9;
const NimBLEUUID kCommandServiceUuid("69400001-b5a3-f393-e0a9-e50e24dcca99");
const NimBLEUUID kWriteCharacteristicUuid("69400002-b5a3-f393-e0a9-e50e24dcca99");

// Literal packets from the reference web client (everlastengineering.com
// ble-dl200.min.js). Trailing byte is a fixed per-type constant, not a
// checksum — never synthesize.
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

// Reference client spaces every write in a sequence by ~100 ms.
constexpr uint32_t kTxGapMs = 90;
// Hold a speed change while the user is still tapping; apply once it settles.
constexpr uint32_t kSpeedSettleMs = 350;

} // namespace

uint16_t DollyDevice::identityColor565() const { return theme::kDollyFill; }
uint16_t DollyDevice::identityTextColor565() const { return theme::kDollyText; }

// Body slab + two wheels (mock icon set).
void DollyDevice::drawGlyph(Adafruit_GFX &g, int16_t x, int16_t y, int16_t s, uint16_t c) const {
	int16_t iw = s * 16 / 26, ih = s * 13 / 26;
	int16_t ix = x + (s - iw) / 2, iy = y + (s - ih) / 2;
	int16_t r = ih * 5 / 26;
	if (r < 2) r = 2;
	g.fillRoundRect(ix, iy, iw, ih * 6 / 13, 1, c);
	g.fillCircle(ix + r + 1, iy + ih - r, r, c);
	g.fillCircle(ix + iw - r - 1, iy + ih - r, r, c);
}

bool DollyDevice::matchesAdvertisement(const NimBLEAdvertisedDevice *adv) const {
	if (!adv->haveName()) return false;
	const std::string name = adv->getName();
	return name.compare(0, 9, "NEEWER-DL") == 0;
}

void DollyDevice::activate() { _active = true; }

void DollyDevice::deactivate() {
	_active = false;
	_moving = false;
	clearQueue();
	if (_connected && _write) writePacket(kStop);
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

	// Reference connect(): SPEED5 -> ... -> SPEED1 with 100 ms gaps. tick()
	// spaces the rest.
	_initializing = true;
	_nextInitSpeed = 4;
	writePacket(kSpeedPackets[4]);
	_nextInitAtMs = millis() + 100;
}

void DollyDevice::onConnect(NimBLEClient *) { _connected = true; }

void DollyDevice::onDisconnect(NimBLEClient *, int) {
	_connected = false;
	_initializing = false;
	_nextInitSpeed = 0;
	_emergencyStep = 0;
	_moving = false;
	_speedDirty = false;
	clearQueue();
	_write = nullptr;
	if (_client) {
		NimBLEDevice::deleteClient(_client);
		_client = nullptr;
	}
}

void DollyDevice::writePacket(const uint8_t *packet) {
	if (_write) _write->writeValue(packet, kPacketSize, false);
}

void DollyDevice::clearQueue() {
	_txHead = 0;
	_txCount = 0;
}

void DollyDevice::enqueue(const uint8_t *packet) {
	if (_txCount >= kTxQueue) {
		_txHead = (_txHead + 1) % kTxQueue;
		_txCount--;
	}
	_tx[(_txHead + _txCount) % kTxQueue] = packet;
	_txCount++;
}

// A move at the current speed is always ACCEL -> SPEEDn -> ENABLEMOTIONxxx,
// spaced by tick() — a bare direction packet ignores speed (ref client).
void DollyDevice::queueMove() {
	_speedDirty = false;
	clearQueue();
	enqueue(kConstantAccel);
	enqueue(kSpeedPackets[_speedLevel - 1]);
	enqueue(_dirRight ? kMoveRight : kMoveLeft);
}

// Reference stop pattern.
void DollyDevice::queueStop() {
	_speedDirty = false;
	clearQueue();
	enqueue(kLiveVideoMode);
	enqueue(kManualMode);
	enqueue(kStop);
}

void DollyDevice::emergencyStop() {
	// Exact sequence from the reference client's emergencystop().
	_initializing = false;
	_moving = false;
	_speedDirty = false;
	clearQueue();
	writePacket(kLiveVideoMode);
	_emergencyStep = 1;
	_nextEmergencyAtMs = millis() + 20;
}

// Emergency burst: LIVEVIDEO, CONSTANTACCEL, STOP, MANUAL x3, then SLOWACCEL.
static const uint8_t *const kEmergencyPackets[] = {
    kLiveVideoMode, kConstantAccel, kStop, kManualMode, kLiveVideoMode, kConstantAccel, kStop,
    kManualMode,    kLiveVideoMode, kConstantAccel, kStop, kManualMode, kSlowAccel,
};
static const uint8_t kEmergencyPacketCount =
    sizeof(kEmergencyPackets) / sizeof(kEmergencyPackets[0]);

void DollyDevice::tick() {
	if (!_connected || !_write) return;
	uint32_t now = millis();

	if (_emergencyStep) {
		if (static_cast<int32_t>(now - _nextEmergencyAtMs) >= 0) {
			writePacket(kEmergencyPackets[_emergencyStep]);
			_emergencyStep++;
			if (_emergencyStep >= kEmergencyPacketCount) _emergencyStep = 0;
			else _nextEmergencyAtMs = now + 20;
		}
		return;
	}

	if (_initializing) {
		if (static_cast<int32_t>(now - _nextInitAtMs) < 0) return;
		writePacket(kSpeedPackets[_nextInitSpeed - 1]);
		if (_nextInitSpeed == 1) {
			_initializing = false;
			_lastTxMs = now;
		} else {
			_nextInitSpeed--;
			_nextInitAtMs = now + 100;
		}
		return;
	}

	// Debounced speed apply: once the user stops tapping and the queue is
	// idle, re-issue ACCEL+SPEED+MOVE so the new level takes effect on the fly.
	if (_speedDirty && _moving && _txCount == 0 &&
	    static_cast<int32_t>(now - _speedApplyAtMs) >= 0) {
		queueMove();
	}

	if (_txCount > 0 && now - _lastTxMs >= kTxGapMs) {
		writePacket(_tx[_txHead]);
		_txHead = (_txHead + 1) % kTxQueue;
		_txCount--;
		_lastTxMs = now;
	}
}

void DollyDevice::handleCommand(Command cmd) {
	if (!_connected || !_write) return;
	if (cmd == Command::EmergencyStop) {
		emergencyStop();
		return;
	}

	switch (cmd) {
	case Command::MoveForward:
		_dirRight = true;
		_moving = true;
		queueMove();
		break;
	case Command::MoveBackward:
		_dirRight = false;
		_moving = true;
		queueMove();
		break;
	case Command::StartProgram:
		_dirRight = true;
		_moving = true;
		queueMove();
		break;
	case Command::StopMove:
	case Command::StopProgram:
		_moving = false;
		queueStop();
		break;
	case Command::SpeedUp:
		if (_speedLevel < 5) _speedLevel++;
		if (_moving) { _speedDirty = true; _speedApplyAtMs = millis() + kSpeedSettleMs; }
		break;
	case Command::SpeedDown:
		if (_speedLevel > 1) _speedLevel--;
		if (_moving) { _speedDirty = true; _speedApplyAtMs = millis() + kSpeedSettleMs; }
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
		tft.printf("Dolly: %s S%u", _moving ? (_dirRight ? "fwd" : "back") : "idle", _speedLevel);
	}
}
