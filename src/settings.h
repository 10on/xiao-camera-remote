#pragma once

#include <Arduino.h>

// Persisted user prefs that survive reboot and can change on-device:
// selected Profile/BindingPreset index (see profile.h) and backlight
// brightness. Everything else about a Profile/BindingPreset is
// compile-time — profile.h explains why there's nothing else to persist.
class Settings {
public:
	void begin(); // loads from NVS (ESP32 Preferences), clamped to current table sizes

	int profileIndex() const { return _profileIndex; }
	int bindingPresetIndex() const { return _bindingPresetIndex; }
	uint8_t brightness() const { return _brightness; }

	// Each setter updates the in-RAM value and writes through to NVS
	// immediately — these only ever fire from menu button presses, so
	// there's no write-frequency reason to debounce.
	void setProfileIndex(int index);
	void setBindingPresetIndex(int index);
	void setBrightness(uint8_t value);

private:
	int _profileIndex = 0;
	int _bindingPresetIndex = 0;
	uint8_t _brightness = 200; // bright but not max, matches the old always-on digitalWrite(HIGH)
};

extern Settings settings;
