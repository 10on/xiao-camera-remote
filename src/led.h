#pragma once

#include <Arduino.h>

#include "config.h"

// Two discrete LEDs on the PCF8575 expander (WS2812 was dropped — not in
// stock, see docs/hardware.md): yellow = power/battery, blue = workflow/
// activity. Both are wired active-LOW (PCF8575 sinks current far better
// than it sources it — see docs/hardware.md), and the expander can't do
// PWM, so there's no dimming — only on/off/blink.
class StatusLed {
public:
	void begin();
	void update(); // drives blink timing; call every loop iteration

	void setPower(bool on, bool blink = false);
	void setActivity(bool on, bool blink = false);

private:
	struct Channel {
		explicit Channel(uint8_t bit) : pcfBit(bit) {}
		uint8_t pcfBit;
		bool on = false;
		bool blink = false;
	};

	void apply(const Channel &ch, bool lit);

	Channel _power{PCF_BIT_LED_YELLOW};
	Channel _activity{PCF_BIT_LED_BLUE};
	uint32_t _lastBlinkMs = 0;
	bool _blinkPhase = false;
};

extern StatusLed statusLed;
