#include "settings.h"
#include <Preferences.h>
#include "profile.h"

Settings settings;

static const char *kNamespace = "settings";
static const char *kKeyProfile = "profile";
static const char *kKeyBinding = "binding";
static const char *kKeyBrightness = "bright";

void Settings::begin() {
	Preferences prefs;
	prefs.begin(kNamespace, /*readOnly=*/true);
	_profileIndex = prefs.getInt(kKeyProfile, 0);
	_bindingPresetIndex = prefs.getInt(kKeyBinding, 0);
	_brightness = prefs.getUChar(kKeyBrightness, 200);
	prefs.end();

	// Defends against a stale NVS value from a build with more entries
	// than the current firmware's tables — a fresh device (nothing
	// written yet) reads the getInt/getUChar defaults above, not 0 by
	// accident of an out-of-range index.
	if (_profileIndex < 0 || _profileIndex >= kProfileCount) _profileIndex = 0;
	if (_bindingPresetIndex < 0 || _bindingPresetIndex >= kBindingPresetCount) _bindingPresetIndex = 0;
}

void Settings::setProfileIndex(int index) {
	_profileIndex = index;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putInt(kKeyProfile, index);
	prefs.end();
}

void Settings::setBindingPresetIndex(int index) {
	_bindingPresetIndex = index;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putInt(kKeyBinding, index);
	prefs.end();
}

void Settings::setBrightness(uint8_t value) {
	_brightness = value;
	Preferences prefs;
	prefs.begin(kNamespace, false);
	prefs.putUChar(kKeyBrightness, value);
	prefs.end();
}
