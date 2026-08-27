#pragma once

#include <Arduino.h>

// Passive piezo buzzer, driven via the core's tone()/noTone() (LEDC PWM
// under the hood on ESP32). Freed up by giving the display a software
// reset instead of a hardware RST pin (see config.h).
class Buzzer {
public:
	void begin();

	// Non-blocking: starts a tone and remembers when to stop it in
	// update(), so callers never block the UI loop waiting on a beep.
	void beep(uint32_t freqHz = 2500, uint32_t durationMs = 40);
	void update();

private:
	uint32_t _stopAtMs = 0;
	bool _active = false;
};

extern Buzzer buzzer;
