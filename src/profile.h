#pragma once

#include "command.h"

// Selectable D-pad -> Command mapping for the Control screen. Indexed by
// ButtonId (Up/Down/Left/Right/Ok — see buttons.h). onLongPress uses
// Command::StopMove as a "nothing bound" sentinel for buttons whose long
// press does nothing, since StopMove is always safe to send twice.
struct BindingPreset {
	const char *name; // Latin placeholder — see docs/screen-design.md's Cyrillic-text note
	Command onPress[5];
	Command onLongPress[5];
};

// Whenever a phone AND a motion device (slider/dolly) are both active at
// once, Ok on the Control screen always toggles both together — start
// fans out MoveForward+Record, stop fans out EmergencyStop+StopRecord —
// regardless of which BindingPreset is selected. This used to be a
// per-preset opt-in (a dedicated "Slider+Phone" preset with a
// hidden okTogglesCombo flag), which meant the natural "activate two
// things, go press the button" flow silently did nothing until you'd
// first dug through Menu -> Profiles -> Bindings to find the right
// preset. Reported as "why do I need a million submenus for something
// this basic" — combo mode is derived from what's actually active
// instead, so it just works the moment both are on. See
// Menu::handleButton's Control case (comboModeActive()).

extern const BindingPreset kBindingPresets[];
extern const int kBindingPresetCount;

// Named set of devices to activate together. `deviceIndices` are indices
// into menu.cpp's kDevices table. Content here is compile-time only —
// hardware-spec.md puts profile *editing* in a future web UI, not
// on-device, so there's nothing for an on-device CRUD/NVS schema to
// manage; only the *selection* (see settings.h) needs to survive reboot.
struct Profile {
	const char *name; // Latin placeholder — see docs/screen-design.md's Cyrillic-text note
	const int *deviceIndices;
	int deviceCount;
};

extern const Profile kProfiles[];
extern const int kProfileCount;
