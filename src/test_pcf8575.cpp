// Standalone PCF8575 + center-button bring-up test.
// - PCF8575: probes the I2C address, prints ACK/NACK.
// - Center/Ok button: direct XIAO GPIO (D3), not on the expander at all
//   — prints on press/release.
// No LEDs/arrow-buttons/display touched.
// Build with: pio run -e pcf_test -t upload

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

static bool lastPressed = false;

void setup() {
	Serial.begin(SERIAL_BAUD);
	Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
	pinMode(PIN_BTN_OK, INPUT_PULLUP);
}

void loop() {
	Wire.beginTransmission(PCF8575_ADDRESS);
	uint8_t err = Wire.endTransmission();
	Serial.printf("[PCF8575] probe 0x%02X: %s (err=%u)\n", PCF8575_ADDRESS,
	              err == 0 ? "ACK" : "NO ACK", err);

	// TEMP: isolate the read failure seen in color_bars (100% "i2cRead
	// returned Error -1") — testing read here with nothing else on the
	// bus/CPU (no SPI, no LEDC PWM) to see if it's a bus problem or an
	// interaction with the display/PWM code.
	size_t got = Wire.requestFrom((uint8_t)PCF8575_ADDRESS, (uint8_t)2);
	if (got == 2) {
		uint8_t lo = Wire.read();
		uint8_t hi = Wire.read();
		Serial.printf("[PCF8575] read OK: 0x%02X%02X\n", hi, lo);
	} else {
		Serial.printf("[PCF8575] read FAILED, got %u bytes\n", (unsigned)got);
	}

	bool pressed = digitalRead(PIN_BTN_OK) == LOW; // active LOW
	if (pressed != lastPressed) {
		lastPressed = pressed;
		Serial.printf("[BTN_OK] %s\n", pressed ? "PRESSED" : "released");
	}

	delay(200);
}
