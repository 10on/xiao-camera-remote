// Standalone display bring-up test — draws classic 7-bar color bars
// (white/yellow/cyan/green/magenta/red/blue), the "телевизионные полосы"
// pattern. Build with: pio run -e color_bars -t upload
// Only display.cpp + pcf8575.cpp are linked alongside this file (see
// platformio.ini) — no buttons/battery/BLE. PCF8575 is needed because
// display RST now lives on it (P10).

#include <Arduino.h>
#include "display.h"
#include "pcf8575.h"
#include "buzzer.h"
#include "battery.h"
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

// TEMP: center/Ok button test — direct XIAO GPIO (D3), not on the
// expander. Drawn as a square in the corner: gray = released, green =
// pressed.
static const int16_t kBtnBoxSize = 40;
static bool lastPressed = false;

static void drawBtnState(Adafruit_ST7789 &tft, bool pressed) {
	uint16_t color = pressed ? 0x07E0 /* green */ : 0x39C7 /* gray */;
	tft.fillRect(0, tft.height() - kBtnBoxSize, kBtnBoxSize, kBtnBoxSize, color);
}

// TEMP: arrow buttons on the PCF8575 — 4 boxes along the bottom-right,
// same gray/green scheme as the center button box.
struct ArrowBtn {
	uint8_t bit;
	const char *name;
};
static const ArrowBtn kArrows[] = {
	{PCF_BIT_BTN_UP, "UP"},
	{PCF_BIT_BTN_DOWN, "DOWN"},
	{PCF_BIT_BTN_LEFT, "LEFT"},
	{PCF_BIT_BTN_RIGHT, "RIGHT"},
};
static const int kArrowCount = sizeof(kArrows) / sizeof(kArrows[0]);
static bool lastArrowPressed[kArrowCount] = {false, false, false, false};

static void drawArrowState(Adafruit_ST7789 &tft, int idx, bool pressed) {
	uint16_t color = pressed ? 0x07E0 /* green */ : 0x39C7 /* gray */;
	int16_t x = tft.width() - (kArrowCount - idx) * (kBtnBoxSize + 4);
	tft.fillRect(x, tft.height() - kBtnBoxSize, kBtnBoxSize, kBtnBoxSize, color);
}

// TEMP: battery voltage/percent, printed on-screen instead of Serial —
// serial monitor over this board's native USB-JTAG is unreliable from
// this environment's non-interactive terminal, so the display is the
// actual debug output for this whole test.
static const int16_t kInfoBarHeight = 20;

static void drawBattery(Adafruit_ST7789 &tft) {
	tft.fillRect(0, 0, tft.width(), kInfoBarHeight, 0x0000); // black strip
	tft.setCursor(4, 4);
	tft.setTextColor(0xFFFF);
	tft.setTextSize(1);
	tft.printf("BATT: %.2fV (%u%%)", battery.voltage(), battery.percent());
}

void setup() {
	Serial.begin(SERIAL_BAUD);
	// display.begin() pulses RST via the PCF8575 — needs I2C up first.
	// INT isn't physically wired yet, harmless to pass anyway (pulled up,
	// never fires, not used by writeBit()).
	pcf8575.begin(PCF8575_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL, PIN_PCF_INT);
	display.begin();
	buzzer.begin();
	battery.begin();
	pinMode(PIN_BTN_OK, INPUT_PULLUP);

	Adafruit_ST7789 &tft = display.tft();
	// width()/height() reflect the rotation set in Display::begin(),
	// not the raw panel dimensions in config.h. Bars start below the
	// battery info strip now (kInfoBarHeight).
	int barWidth = tft.width() / kBarCount;
	for (int i = 0; i < kBarCount; i++) {
		tft.fillRect(i * barWidth, kInfoBarHeight, barWidth, tft.height() - kInfoBarHeight, kBars[i]);
	}
	drawBtnState(tft, false);
	for (int i = 0; i < kArrowCount; i++) {
		drawArrowState(tft, i, false);
	}
	drawBattery(tft);
}

void loop() {
	// TEMP: cyclic brightness test now that BL is a real PWM-capable GPIO
	// (D2) instead of hardwired to 3V3 — sweeps 0->255->0 continuously.
	static int level = 0;
	static int step = 5;
	analogWrite(PIN_TFT_BL, level);
	level += step;
	if (level >= 255 || level <= 0) step = -step;

	bool pressed = digitalRead(PIN_BTN_OK) == LOW; // active LOW
	if (pressed != lastPressed) {
		lastPressed = pressed;
		Serial.printf("[BTN_OK] %s\n", pressed ? "PRESSED" : "released");
		drawBtnState(display.tft(), pressed);
	}

	// Arrow buttons: no INT wired up yet, just poll the port every loop
	// tick (fine for this test — buttons.cpp does it properly via INT).
	uint16_t bits = pcf8575.read();
	for (int i = 0; i < kArrowCount; i++) {
		bool arrowPressed = (bits & (1 << kArrows[i].bit)) == 0; // active LOW
		if (arrowPressed != lastArrowPressed[i]) {
			lastArrowPressed[i] = arrowPressed;
			Serial.printf("[BTN_%s] %s\n", kArrows[i].name, arrowPressed ? "PRESSED" : "released");
			drawArrowState(display.tft(), i, arrowPressed);
		}
	}

	// TEMP: LED test — alternate yellow/blue every 500ms, active-LOW.
	static uint32_t lastBlinkMs = 0;
	static bool yellowOn = false;
	uint32_t now = millis();
	if (now - lastBlinkMs >= 500) {
		lastBlinkMs = now;
		yellowOn = !yellowOn;
		pcf8575.writeBit(PCF_BIT_LED_YELLOW, !yellowOn);
		pcf8575.writeBit(PCF_BIT_LED_BLUE, yellowOn);
	}

	// TEMP: buzzer test — short beep once a second at the confirmed
	// loudest frequency for this speaker (2-3kHz range, picked 2500Hz).
	static uint32_t lastBeepMs = 0;
	if (now - lastBeepMs >= 1000) {
		lastBeepMs = now;
		buzzer.beep();
	}
	buzzer.update();

	// TEMP: battery ADC test — print voltage/percent every 2s. battery.cpp
	// itself only actually samples every 5s internally, this just prints
	// whatever it currently has.
	static uint32_t lastBattPrintMs = 0;
	battery.update();
	if (now - lastBattPrintMs >= 2000) {
		lastBattPrintMs = now;
		drawBattery(display.tft());
	}

	delay(15);
}
