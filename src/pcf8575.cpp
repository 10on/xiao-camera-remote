#include "pcf8575.h"
#include <Wire.h>

Pcf8575 pcf8575;

volatile bool Pcf8575::_changed = true; // force an initial read on first update()

void IRAM_ATTR Pcf8575::onInterrupt() {
	_changed = true;
}

void Pcf8575::begin(uint8_t address, int sdaPin, int sclPin, int intPin) {
	_address = address;
	Wire.begin(sdaPin, sclPin);

	Wire.beginTransmission(_address);
	uint8_t ackErr = Wire.endTransmission();
	Serial.printf("[PCF8575] probe 0x%02X: %s (err=%u)\n", _address,
	              ackErr == 0 ? "ACK" : "NO ACK", ackErr);

	writeShadow(); // all pins high: button inputs get their pull-up, LEDs/RST off

	// INT is open-drain — INPUT_PULLUP as a safety net in case the
	// expander breakout doesn't already have one (see docs/hardware.md).
	pinMode(intPin, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(intPin), onInterrupt, FALLING);
}

bool Pcf8575::changed() {
	if (!_changed) return false;
	_changed = false;
	return true;
}

uint16_t Pcf8575::read() {
	Wire.requestFrom(_address, (uint8_t)2);
	if (Wire.available() < 2) return _shadow; // I2C hiccup — hold last known state
	uint16_t lo = Wire.read();
	uint16_t hi = Wire.read();
	return static_cast<uint16_t>(lo | (hi << 8));
}

void Pcf8575::writeBit(uint8_t bit, bool high) {
	if (high) _shadow |= static_cast<uint16_t>(1u << bit);
	else _shadow &= static_cast<uint16_t>(~(1u << bit));
	writeShadow();
}

void Pcf8575::writeShadow() {
	Wire.beginTransmission(_address);
	Wire.write(static_cast<uint8_t>(_shadow & 0xFF));
	Wire.write(static_cast<uint8_t>((_shadow >> 8) & 0xFF));
	uint8_t err = Wire.endTransmission();
	if (err != 0) {
		Serial.printf("[PCF8575] write FAILED, shadow=0x%04X, err=%u\n", _shadow, err);
	}
}
