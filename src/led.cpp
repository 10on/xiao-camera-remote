#include "led.h"
#include "pcf8575.h"

StatusLed statusLed;

static const uint32_t kBlinkIntervalMs = 400;

void StatusLed::begin() {
	apply(_power, false);
	apply(_activity, false);
}

void StatusLed::setPower(bool on, bool blink) {
	_power.on = on;
	_power.blink = blink;
}

void StatusLed::setActivity(bool on, bool blink) {
	_activity.on = on;
	_activity.blink = blink;
}

void StatusLed::apply(const Channel &ch, bool lit) {
	// Active-LOW: driving the pin low sinks current through the LED.
	pcf8575.writeBit(ch.pcfBit, !lit);
}

void StatusLed::update() {
	uint32_t now = millis();
	if (now - _lastBlinkMs >= kBlinkIntervalMs) {
		_lastBlinkMs = now;
		_blinkPhase = !_blinkPhase;
	}

	apply(_power, _power.on && (!_power.blink || _blinkPhase));
	apply(_activity, _activity.on && (!_activity.blink || _blinkPhase));
}
