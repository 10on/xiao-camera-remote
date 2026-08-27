#include "buzzer.h"
#include "config.h"

Buzzer buzzer;

void Buzzer::begin() {
	pinMode(PIN_BUZZER, OUTPUT);
}

void Buzzer::beep(uint32_t freqHz, uint32_t durationMs) {
	tone(PIN_BUZZER, freqHz);
	_active = true;
	_stopAtMs = millis() + durationMs;
}

void Buzzer::update() {
	if (_active && millis() >= _stopAtMs) {
		noTone(PIN_BUZZER);
		_active = false;
	}
}
