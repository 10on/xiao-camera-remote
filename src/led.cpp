#include "led.h"
#include "config.h"

StatusLed statusLed;

StatusLed::StatusLed() : _pixel(RGB_LED_COUNT, PIN_RGB_LED, NEO_GRB + NEO_KHZ800) {}

void StatusLed::begin() {
	_pixel.begin();
	_pixel.setBrightness(RGB_LED_BRIGHTNESS);
	off();
	_pixel.show();
}

void StatusLed::set(uint8_t r, uint8_t g, uint8_t b) {
	_pixel.setPixelColor(0, _pixel.Color(r, g, b));
	_pixel.show();
}
