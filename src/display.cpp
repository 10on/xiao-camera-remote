#include "display.h"
#include "config.h"
#include "pcf8575.h"
#include "settings.h"
#include <SPI.h>
#include <initializer_list>

Display display;

Display::Display()
	// Canvas is allocated already in the panel's post-rotation (landscape)
	// dimensions — TFT_HEIGHT×TFT_WIDTH, not TFT_WIDTH×TFT_HEIGHT — so its
	// width()/height() match _tft's after setRotation(1) in begin(), and
	// every caller (menu.cpp, theme.h) can keep using width()/height()
	// without caring which one it's actually drawing into.
	: _tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST), _canvas(TFT_HEIGHT, TFT_WIDTH) {}

// This panel's real controller is NV3030B, not ST7789 (see docs/hardware.md)
// — Adafruit_ST7789::init() only sends the bare MIPI-DCS minimum (reset,
// sleep-out, color mode, addressing, inversion, display-on) and otherwise
// relies on the silicon's power-on-reset defaults for gamma/VCOM/pump/porch.
// Real ST7789 panels ship OTP-trimmed for that; this NV3030B panel doesn't,
// which is why the picture looked washed out/foggy (raised black level,
// low contrast) instead of outright broken. These are the actual tuning
// registers from a from-scratch NV3030B driver written against this exact
// Waveshare 1.5" 240x280 module (github.com/sangyuxiaowu/NV3030B), replayed
// here as raw MIPI-DCS command/data writes since Adafruit_ST7789 has no
// hook for a panel-specific init table. 0xFD brackets them as a
// vendor-register unlock/lock, matching that reference's ordering.
static void tuneNv3030b(Adafruit_ST7789 &tft) {
	auto cmd = [&](uint8_t c, std::initializer_list<uint8_t> data = {}) {
		tft.sendCommand(c, data.begin(), data.size());
	};

	cmd(0xFD, {0x06, 0x08}); // vendor register unlock

	cmd(0x61, {0x07, 0x04});
	cmd(0x62, {0x00, 0x44, 0x45});
	cmd(0x63, {0x41, 0x07, 0x12, 0x12});
	cmd(0x64, {0x37});

	cmd(0x65, {0x09, 0x10, 0x21}); // pump1 = 4.7MHz, VSP
	cmd(0x66, {0x09, 0x10, 0x21}); // pump2, AVCL
	cmd(0x67, {0x21, 0x40});       // pump select
	cmd(0x68, {0x90, 0x4c, 0x50, 0x70}); // gamma VAP/VAN

	cmd(0xB1, {0x0F, 0x02, 0x01}); // frame rate
	cmd(0xB4, {0x01});             // layout control: 1dot
	cmd(0xB5, {0x02, 0x02, 0x0a, 0x14}); // porch
	cmd(0xB6, {0x04, 0x01, 0x9f, 0x00, 0x02}); // gate control

	cmd(0xDF, {0x11}); // gamma curve select
	cmd(0xE2, {0x03, 0x00, 0x00, 0x30, 0x33, 0x3f});
	cmd(0xE5, {0x3f, 0x33, 0x30, 0x00, 0x00, 0x03});
	cmd(0xE1, {0x05, 0x67});
	cmd(0xE4, {0x67, 0x06});
	cmd(0xE0, {0x05, 0x06, 0x0A, 0x0C, 0x0B, 0x0B, 0x13, 0x19});
	cmd(0xE3, {0x18, 0x13, 0x0D, 0x09, 0x0B, 0x0B, 0x05, 0x06});

	cmd(0xE6, {0x00, 0xff});
	cmd(0xE7, {0x01, 0x04, 0x03, 0x03, 0x00, 0x12});
	cmd(0xE8, {0x00, 0x70, 0x00});
	cmd(0xEC, {0x52});

	cmd(0xF1, {0x01, 0x01, 0x02});
	cmd(0xF6, {0x01, 0x30, 0x00, 0x00});

	cmd(0xFD, {0xfa, 0xfc}); // vendor register lock
}

void Display::begin() {
#ifdef PIN_TFT_BL
	// PWM, not a plain on/off digitalWrite: BL was repinned from 3V3 onto
	// this GPIO specifically to be dimmable (config.h) — analogWrite()
	// drives it through the LEDC peripheral, same mechanism buzzer.cpp's
	// tone()/noTone() already uses on a different pin, so no extra
	// channel/timer setup needed here.
	analogWrite(PIN_TFT_BL, settings.brightness());
#endif

	// RST lives on the PCF8575 (P10) now, not a XIAO GPIO. PCF8575 pins
	// power up HIGH via their weak pull-up, so nothing pulses RST low
	// automatically — do it explicitly here. pcf8575.begin() must already
	// have run before this is called.
	pcf8575.writeBit(PCF_BIT_DISP_RST, false);
	delay(20);
	pcf8575.writeBit(PCF_BIT_DISP_RST, true);
	delay(120); // controller's post-reset settle time before accepting commands

	SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
	// init(240, 280) auto-selects the centered 1.69"-family offset
	// (rowstart=20, colstart=0) matching this panel — see
	// Adafruit_ST7789::init() in the library.
	_tft.init(TFT_WIDTH, TFT_HEIGHT);
	tuneNv3030b(_tft);
	// Raised back toward the original 40MHz now that the actual noise
	// causes are fixed (CLK/DIN pin mixup, a solder bridge on the board) —
	// the earlier "drop the speed" workaround was masking those, not
	// fixing a genuine signal-integrity limit. Landed on 20MHz as a
	// margin-of-safety middle ground on breadboard jumpers; try 40MHz
	// again once wiring moves to something more solid (short wires/PCB).
	_tft.setSPISpeed(20000000);
	_tft.setRotation(1); // landscape, 90° from panel's native portrait
	clear();
	flush(); // physically blank the panel now, instead of showing stale GRAM content
}

void Display::clear(uint16_t color) {
	_canvas.fillScreen(color);
}

void Display::flush() {
	// One SPI transaction for the whole composited frame, instead of the
	// many small immediate draw calls every render*() makes against the
	// canvas — see the class comment in display.h for why that matters.
	_tft.startWrite();
	_tft.setAddrWindow(0, 0, _tft.width(), _tft.height());
	_tft.writePixels(_canvas.getBuffer(), (uint32_t)_tft.width() * _tft.height());
	_tft.endWrite();
}

void Display::setBrightness(uint8_t value) {
#ifdef PIN_TFT_BL
	analogWrite(PIN_TFT_BL, value);
#endif
}
