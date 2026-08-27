// Absolute minimal sketch — just prints a counter over Serial, nothing
// else touched (no display/I2C/ADC/PCF8575). Used to check whether
// serial output works at all when everything else is stripped out.
// Build with: pio run -e hello_test -t upload

#include <Arduino.h>

void setup() {
	Serial.begin(115200);
}

void loop() {
	static uint32_t n = 0;
	Serial.printf("hello %lu\n", (unsigned long)n++);
	delay(500);
}
