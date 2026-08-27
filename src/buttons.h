#pragma once

#include <Arduino.h>

enum class ButtonId : uint8_t {
	Up = 0,
	Down,
	Left,
	Right,
	Ok,
	Count
};

enum class ButtonEvent : uint8_t {
	None = 0,
	Press,      // short press, fires on release
	LongPress,  // fires once when held past BUTTON_LONGPRESS_MS
};

// Polled, debounced 5-button reader. Call update() every loop iteration,
// then poll(id) once per button to consume any pending event.
class Buttons {
public:
	void begin();
	void update();
	ButtonEvent poll(ButtonId id);

private:
	struct State {
		uint8_t pin;
		bool stableLevel = true;   // true = released (active LOW)
		bool lastRaw = true;
		uint32_t lastChangeMs = 0;
		bool longFired = false;
		ButtonEvent pending = ButtonEvent::None;
	};

	State _btn[static_cast<size_t>(ButtonId::Count)];
};

extern Buttons buttons;
