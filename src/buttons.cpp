#include "buttons.h"
#include "config.h"
#include "pcf8575.h"

Buttons buttons;

void Buttons::begin() {
	pinMode(PIN_BTN_OK, INPUT_PULLUP);
	for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); i++) {
		_btn[i].stableLevel = true;
		_btn[i].lastRaw = true;
	}
}

bool Buttons::readRaw(ButtonId id) const {
	switch (id) {
	case ButtonId::Up:    return (_expanderBits & (1 << PCF_BIT_BTN_UP)) != 0;
	case ButtonId::Down:  return (_expanderBits & (1 << PCF_BIT_BTN_DOWN)) != 0;
	case ButtonId::Left:  return (_expanderBits & (1 << PCF_BIT_BTN_LEFT)) != 0;
	case ButtonId::Right: return (_expanderBits & (1 << PCF_BIT_BTN_RIGHT)) != 0;
	case ButtonId::Ok:    return digitalRead(PIN_BTN_OK) != 0;
	default:              return true;
	}
}

void Buttons::update() {
	uint32_t now = millis();

	// Only re-read the expander over I2C when its INT line says something
	// changed — avoids constant I2C traffic/latency from polling every
	// loop tick (see docs/hardware.md). Ok/center doesn't need this, it's
	// a plain GPIO read.
	if (pcf8575.changed()) {
		_expanderBits = pcf8575.read();
	}

	for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); i++) {
		State &b = _btn[i];
		bool raw = readRaw(static_cast<ButtonId>(i));

		if (raw != b.lastRaw) {
			b.lastRaw = raw;
			b.lastChangeMs = now;
		}

		if ((now - b.lastChangeMs) >= BUTTON_DEBOUNCE_MS && raw != b.stableLevel) {
			b.stableLevel = raw;
			if (b.stableLevel == false) {
				// just pressed
				b.longFired = false;
			} else {
				// just released
				if (!b.longFired) {
					b.pending = ButtonEvent::Press;
				}
			}
		}

		if (b.stableLevel == false && !b.longFired &&
		    (now - b.lastChangeMs) >= BUTTON_LONGPRESS_MS) {
			b.longFired = true;
			b.pending = ButtonEvent::LongPress;
		}
	}
}

ButtonEvent Buttons::poll(ButtonId id) {
	State &b = _btn[static_cast<size_t>(id)];
	ButtonEvent ev = b.pending;
	b.pending = ButtonEvent::None;
	return ev;
}
