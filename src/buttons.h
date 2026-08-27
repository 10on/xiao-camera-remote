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

// Debounced 5-button reader. Up/Down/Left/Right are read from the PCF8575
// expander; Ok (the center/wake button) is a dedicated XIAO GPIO — see
// config.h and docs/hardware.md for why. Call update() every loop
// iteration, then poll(id) once per button to consume any pending event.
class Buttons {
public:
	void begin();
	void update();
	ButtonEvent poll(ButtonId id);

private:
	struct State {
		bool stableLevel = true;   // true = released (active LOW)
		bool lastRaw = true;
		uint32_t lastChangeMs = 0;
		bool longFired = false;
		ButtonEvent pending = ButtonEvent::None;
	};

	bool readRaw(ButtonId id) const;

	State _btn[static_cast<size_t>(ButtonId::Count)];
	uint16_t _expanderBits = 0xFFFF; // cached PCF8575 port, refreshed on its INT
};

extern Buttons buttons;
