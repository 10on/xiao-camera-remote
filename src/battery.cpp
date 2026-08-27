#include "battery.h"
#include "config.h"

Battery battery;

static const uint32_t kReadIntervalMs = 5000;

void Battery::begin() {
	pinMode(PIN_BATT_ADC, INPUT);
	update();
}

void Battery::update() {
	uint32_t now = millis();
	if (_lastReadMs != 0 && (now - _lastReadMs) < kReadIntervalMs) {
		return;
	}
	_lastReadMs = now;

	uint32_t raw = analogReadMilliVolts(PIN_BATT_ADC);
	_voltage = (raw / 1000.0f) * BATT_DIVIDER_RATIO;

	float clamped = constrain(_voltage, BATT_EMPTY_V, BATT_FULL_V);
	_percent = static_cast<uint8_t>(
		((clamped - BATT_EMPTY_V) / (BATT_FULL_V - BATT_EMPTY_V)) * 100.0f);
}
