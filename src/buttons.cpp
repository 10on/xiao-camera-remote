#include "buttons.h"
#include "config.h"

Buttons buttons;

static const uint8_t kPins[] = {
	PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_OK,
};

void Buttons::begin() {
	for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); i++) {
		_btn[i].pin = kPins[i];
		pinMode(_btn[i].pin, INPUT_PULLUP);
		_btn[i].stableLevel = true;
		_btn[i].lastRaw = true;
	}
}

void Buttons::update() {
	uint32_t now = millis();

	for (size_t i = 0; i < static_cast<size_t>(ButtonId::Count); i++) {
		State &b = _btn[i];
		bool raw = digitalRead(b.pin);

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
