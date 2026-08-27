// Standalone display bring-up test — draws classic 7-bar color bars
// (white/yellow/cyan/green/magenta/red/blue), the "телевизионные полосы"
// pattern. Build with: pio run -e color_bars -t upload
// Only display.cpp is linked alongside this file (see platformio.ini),
// so it doesn't touch buttons/battery/BLE at all.

#include <Arduino.h>
#include "display.h"
#include "config.h"

static const uint16_t kBars[] = {
	0xFFFF, // white
	0xFFE0, // yellow
	0x07FF, // cyan
	0x07E0, // green
	0xF81F, // magenta
	0xF800, // red
	0x001F, // blue
};
static const int kBarCount = sizeof(kBars) / sizeof(kBars[0]);

void setup() {
	Serial.begin(SERIAL_BAUD);
	display.begin();

	Adafruit_ST7789 &tft = display.tft();
	// width()/height() reflect the rotation set in Display::begin(),
	// not the raw panel dimensions in config.h.
	int barWidth = tft.width() / kBarCount;
	for (int i = 0; i < kBarCount; i++) {
		tft.fillRect(i * barWidth, 0, barWidth, tft.height(), kBars[i]);
	}
}

void loop() {
}
