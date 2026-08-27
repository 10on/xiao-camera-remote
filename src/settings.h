#pragma once

#include <Arduino.h>

// Persisted user prefs that survive reboot and can change on-device:
// last-used rig index (see rig.h), autostart flag, max speed cap, key
// sound, and backlight brightness. Everything else about a Rig is
// compile-time — rig.h explains why there's nothing else to persist yet.
class Settings {
public:
	void begin(); // loads from NVS (ESP32 Preferences)

	int rigIndex() const { return _rigIndex; }
	bool autostartLastRig() const { return _autostartLastRig; }
	uint8_t maxSpeedLevel() const { return _maxSpeedLevel; } // 1..8 cap on the control screen
	bool buttonSound() const { return _buttonSound; }
	uint8_t brightness() const { return _brightness; }

	// Each setter updates the in-RAM value and writes through to NVS
	// immediately — these only ever fire from menu button presses.
	void setRigIndex(int index);
	void setAutostartLastRig(bool enabled);
	void setMaxSpeedLevel(uint8_t level);
	void setButtonSound(bool enabled);
	void setBrightness(uint8_t value);

	// Wipes every persisted namespace (rigs, aliases, prefs). Caller reboots.
	void factoryReset();

private:
	int _rigIndex = 0;
	bool _autostartLastRig = false;
	uint8_t _maxSpeedLevel = 8;
	bool _buttonSound = true;
	uint8_t _brightness = 200; // bright but not max, matches the old always-on digitalWrite(HIGH)
};

extern Settings settings;
