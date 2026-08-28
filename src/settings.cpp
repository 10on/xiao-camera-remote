#include "settings.h"
#include <Preferences.h>

Settings settings;

static const char *kNamespace = "settings";
static const char *kKeyRig = "rig";
static const char *kKeyAutostart = "autorig";
static const char *kKeyMaxSpeed = "maxspdpct"; // was "maxspd" (a 1..8 level); key renamed so old values don't read back as an 8% cap
static const char *kKeyKeySound = "keysnd";
static const char *kKeyBrightness = "bright";

void Settings::begin() {
	Preferences prefs;
	// Read-write (not RO): on a fresh device the namespace doesn't exist yet
	// and an RO open logs a scary "nvs_open failed: NOT_FOUND" every boot.
	// RW creates it empty; getX() still returns the defaults below.
	prefs.begin(kNamespace, /*readOnly=*/false);
	_rigIndex = prefs.getInt(kKeyRig, 0);
	_autostartLastRig = prefs.getBool(kKeyAutostart, false);
	_maxSpeedPercent = prefs.getUChar(kKeyMaxSpeed, 100);
	_buttonSound = prefs.getBool(kKeyKeySound, true);
	_brightness = prefs.getUChar(kKeyBrightness, 200);
	prefs.end();

	if (_rigIndex < 0) _rigIndex = 0;
	// Snap to the 10% grid the control screen steps on; keep at least one step.
	_maxSpeedPercent = (uint8_t)constrain((_maxSpeedPercent + 5) / 10 * 10, 10, 100);
}

void Settings::setRigIndex(int index) {
	_rigIndex = index;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putInt(kKeyRig, index);
	prefs.end();
}


void Settings::setAutostartLastRig(bool enabled) {
	_autostartLastRig = enabled;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putBool(kKeyAutostart, enabled);
	prefs.end();
}

void Settings::setMaxSpeedPercent(uint8_t percent) {
	_maxSpeedPercent = (uint8_t)constrain((int)percent, 10, 100);
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putUChar(kKeyMaxSpeed, _maxSpeedPercent);
	prefs.end();
}

void Settings::setButtonSound(bool enabled) {
	_buttonSound = enabled;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putBool(kKeyKeySound, enabled);
	prefs.end();
}

void Settings::setBrightness(uint8_t value) {
	_brightness = value;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putUChar(kKeyBrightness, value);
	prefs.end();
}

void Settings::factoryReset() {
	for (const char *ns : {"settings", "rigs", "devreg"}) {
		Preferences prefs;
		prefs.begin(ns, false);
		prefs.clear();
		prefs.end();
	}
}
