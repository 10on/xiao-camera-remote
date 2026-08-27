#pragma once

#include <Arduino.h>

// Which axis carries speed on the Main-centric control screen. Global, not
// per-rig — ux-redesign.md §11 puts it in the "Управление" settings section.
// The chosen mode is always labelled on the control screen (§7).
enum class AxisBinding : uint8_t {
	SpeedUpDown = 0,    // ↑↓ = speed, ←→ = move
	SpeedLeftRight = 1, // ←→ = speed, ↑↓ = move
};

// Persisted user prefs that survive reboot and can change on-device:
// last-used rig index (see rig.h), axis binding, autostart flag, and
// backlight brightness. Everything else about a Rig is compile-time —
// rig.h explains why there's nothing else to persist yet.
class Settings {
public:
	void begin(); // loads from NVS (ESP32 Preferences), clamped to current table sizes

	int rigIndex() const { return _rigIndex; }
	AxisBinding axisBinding() const { return _axisBinding; }
	bool autostartLastRig() const { return _autostartLastRig; }
	uint8_t maxSpeedLevel() const { return _maxSpeedLevel; } // 1..8 cap on the control screen
	bool buttonSound() const { return _buttonSound; }
	uint8_t brightness() const { return _brightness; }

	// Each setter updates the in-RAM value and writes through to NVS
	// immediately — these only ever fire from menu button presses, so
	// there's no write-frequency reason to debounce.
	void setRigIndex(int index);
	void setAxisBinding(AxisBinding binding);
	void setAutostartLastRig(bool enabled);
	void setMaxSpeedLevel(uint8_t level);
	void setButtonSound(bool enabled);
	void setBrightness(uint8_t value);

	// Wipes every persisted namespace (rigs, aliases, prefs) — mock 23's
	// "Factory reset". Caller reboots afterwards.
	void factoryReset();

private:
	int _rigIndex = 0;
	AxisBinding _axisBinding = AxisBinding::SpeedUpDown;
	bool _autostartLastRig = false; // ux-redesign.md §4: off by default
	uint8_t _maxSpeedLevel = 8;
	bool _buttonSound = true;
	uint8_t _brightness = 200; // bright but not max, matches the old always-on digitalWrite(HIGH)
};

extern Settings settings;
