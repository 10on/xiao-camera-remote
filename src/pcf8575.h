#pragma once

#include <Arduino.h>

// Minimal driver for a PCF8575 16-bit I2C I/O expander (Up/Down/Left/Right
// buttons, the 2 status LEDs, and the display RST line all live on it —
// see config.h and docs/hardware.md). Pins are quasi-bidirectional:
// writing 1 leaves a pin as a weak-pulled-up input, writing 0 drives it
// low (sink). Every I2C transaction addresses the full 16-bit port at
// once, so this class keeps a shadow register and always re-sends all 16
// bits.
class Pcf8575 {
public:
	// intPin is the expander's INT line (open-drain, active LOW, pulses
	// whenever an input-side bit changes — including bits this class
	// itself just wrote, which is harmless, just a slightly wasted read).
	void begin(uint8_t address, int sdaPin, int sclPin, int intPin);

	// Full 16-bit port read (button inputs and currently-driven LED/RST
	// bits read back identically — that's how quasi-bidirectional pins
	// work). Also clears the expander's own INT line.
	uint16_t read();

	// True once if INT has fired since the last call — callers should
	// treat this as "read() again", not poll read() every loop tick.
	bool changed();

	// Sets one bit in the shadow register and writes the full port.
	// `high` = pin released (weak pull-up) — for the status LEDs, which
	// are wired active-LOW, pass high=false to light one.
	void writeBit(uint8_t bit, bool high);

private:
	uint8_t _address = 0;
	uint16_t _shadow = 0xFFFF; // all pins high: button inputs pulled up, LEDs/RST off

	void writeShadow();

	static void IRAM_ATTR onInterrupt();
	static volatile bool _changed;
};

extern Pcf8575 pcf8575;
