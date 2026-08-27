#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Thin wrapper around the panel driver so the rest of the firmware
// depends on one header, not the display library directly.
//
// All drawing goes through canvas() (an offscreen GFXcanvas16), never
// tft() directly — the panel has no framebuffer of its own, so streaming
// individual small draw calls (erase a rect, then a separate text draw,
// etc.) straight to it is visible on the glass mid-update: partial-redraw
// tearing, felt as jitter on every screen refresh. Composite a full frame
// into RAM first, then flush() it to the panel in one SPI transaction.
class Display {
public:
	Display();

	void begin();
	Adafruit_ST7789 &tft() { return _tft; }   // raw panel access — flush()/setBrightness() only
	GFXcanvas16 &canvas() { return _canvas; } // draw here

	void clear(uint16_t color = 0x0000); // clears the canvas, not the panel — flush() to show it
	void setBrightness(uint8_t value);   // live PWM update, e.g. from the Яркость adjuster
	void flush();                        // push the composited canvas to the panel in one shot

private:
	Adafruit_ST7789 _tft;
	GFXcanvas16 _canvas;
};

extern Display display;
