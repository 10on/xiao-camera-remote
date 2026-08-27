#pragma once

#include <Arduino.h>

// Reads pack voltage through a resistor divider on PIN_BATT_ADC.
// Pack is 2P Li-Po (two cells in parallel — same nominal voltage as one cell,
// doubled capacity), so the single-cell voltage curve applies directly.
class Battery {
public:
	void begin();
	void update();

	float voltage() const { return _voltage; }
	uint8_t percent() const { return _percent; }

private:
	float _voltage = 0.0f;
	uint8_t _percent = 0;
	uint32_t _lastReadMs = 0;
};

extern Battery battery;
