#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Thin wrapper around the panel driver so the rest of the firmware
// depends on one header, not the display library directly.
class Display {
public:
	Display();

	void begin();
	Adafruit_ST7789 &tft() { return _tft; }

	void clear(uint16_t color = 0x0000);

private:
	Adafruit_ST7789 _tft;
};

extern Display display;
