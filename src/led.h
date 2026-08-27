#pragma once

#include <Adafruit_NeoPixel.h>

// Single WS2812 status pixel — freed up by tying the display's CS
// straight to GND instead of a GPIO (see config.h).
class StatusLed {
public:
	StatusLed();

	void begin();

	void set(uint8_t r, uint8_t g, uint8_t b);
	void off() { set(0, 0, 0); }

private:
	Adafruit_NeoPixel _pixel;
};

extern StatusLed statusLed;
