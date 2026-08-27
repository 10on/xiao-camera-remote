#include "settings.h"
#include <Preferences.h>

Settings settings;

static const char *kNamespace = "settings";
static const char *kKeyRig = "rig";
static const char *kKeyAutostart = "autorig";
static const char *kKeyMaxSpeed = "maxspd";
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
	_maxSpeedLevel = prefs.getUChar(kKeyMaxSpeed, 8);
	_buttonSound = prefs.getBool(kKeyKeySound, true);
	_brightness = prefs.getUChar(kKeyBrightness, 200);
	prefs.end();

	if (_rigIndex < 0) _rigIndex = 0;
	if (_maxSpeedLevel < 1 || _maxSpeedLevel > 8) _maxSpeedLevel = 8;
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

void Settings::setMaxSpeedLevel(uint8_t level) {
	if (level < 1) level = 1;
	if (level > 8) level = 8;
	_maxSpeedLevel = level;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putUChar(kKeyMaxSpeed, level);
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
