#include "display.h"
#include "config.h"
#include <SPI.h>

Display display;

Display::Display()
	: _tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST) {}

void Display::begin() {
#ifdef PIN_TFT_BL
	pinMode(PIN_TFT_BL, OUTPUT);
	digitalWrite(PIN_TFT_BL, HIGH);
#endif

	SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
	// init(240, 280) auto-selects the centered 1.69"-family offset
	// (rowstart=20, colstart=0) matching this panel — see
	// Adafruit_ST7789::init() in the library.
	_tft.init(TFT_WIDTH, TFT_HEIGHT);
	_tft.setSPISpeed(40000000);
	_tft.setRotation(1); // landscape, 90° from panel's native portrait
	clear();
}

void Display::clear(uint16_t color) {
	_tft.fillScreen(color);
}
