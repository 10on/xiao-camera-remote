#include "dvh_link.h"

#include <string.h>

#include "devices/phone_device.h"

DvhLink dvhLink;

void DvhLink::setup(NimBLEServer *server) {
	NimBLEService *svc = server->createService(kServiceUuid);
	_event = svc->createCharacteristic(kEventUuid, NIMBLE_PROPERTY::NOTIFY);
	_display = svc->createCharacteristic(kDisplayUuid,
	                                     NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
	_event->setCallbacks(this);
	_display->setCallbacks(this);
}

void DvhLink::tick() {
	if (_helloPending) {
		_helloPending = false;
		send("hello");
	}
}

void DvhLink::send(const char *word) {
	uint16_t h = _mac;
	if (!_event || h == kNone) return;
	_event->notify(reinterpret_cast<const uint8_t *>(word), strlen(word), h);
}

void DvhLink::take() {
	_inTake = true;
	send("take");
}

void DvhLink::stop() {
	if (!_inTake) return;
	_inTake = false;
	send("stop");
}

void DvhLink::onPeerDisconnected(uint16_t connHandle) {
	if (_mac == connHandle) _mac = kNone;
}

// Runs on the NimBLE host task: only record state, notify from tick().
void DvhLink::onSubscribe(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo, uint16_t subValue) {
	if (chr != _event) return;
	uint16_t h = connInfo.getConnHandle();
	if (subValue & 1) {
		_mac = h;
		phoneDevice.onPeerDisconnected(h); // in case it was ever counted as a phone
		_helloPending = true;
	} else if (_mac == h) {
		_mac = kNone;
	}
}

void DvhLink::onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) {
	// Display (key=value lines, docs/remote-ble.md): the remote shows nothing
	// for now — accept the write so dvh gets no error, and ignore it.
	(void)chr;
}
