#include "profile.h"

// clang-format off
const BindingPreset kBindingPresets[] = {
	{
		"Speed U/D", // default: matches the mapping Control originally hardcoded
		// Up               Down                Left                   Right                 Ok
		{Command::SpeedUp,  Command::SpeedDown,  Command::MoveBackward, Command::MoveForward, Command::StopMove},
		{Command::StopMove, Command::StopMove,   Command::StopMove,     Command::StopMove,    Command::Home},
	},
	{
		"Speed L/R", // axes swapped: Left/Right adjusts speed, Up/Down moves
		{Command::MoveBackward, Command::MoveForward, Command::SpeedDown, Command::SpeedUp,  Command::StopMove},
		{Command::StopMove,     Command::StopMove,    Command::StopMove,  Command::StopMove, Command::Home},
	},
	{
		"Dolly", // long-Ok is EmergencyStop, not Home — DollyDevice doesn't implement Home
		{Command::SpeedUp,  Command::SpeedDown, Command::MoveBackward, Command::MoveForward, Command::StopMove},
		{Command::StopMove, Command::StopMove,  Command::StopMove,     Command::StopMove,    Command::EmergencyStop},
	},
};
// clang-format on
const int kBindingPresetCount = sizeof(kBindingPresets) / sizeof(kBindingPresets[0]);

// Indices into menu.cpp's kDevices: sliderDevice=0, phoneDevice=1, dollyDevice=2.
static const int kSliderProfileDevices[] = {0};
static const int kPhoneProfileDevices[] = {1};
static const int kSliderPhoneProfileDevices[] = {0, 1};
static const int kDollyProfileDevices[] = {2};
static const int kDollyPhoneProfileDevices[] = {1, 2};

const Profile kProfiles[] = {
	{"Slider", kSliderProfileDevices, 1},
	{"Phone", kPhoneProfileDevices, 1},
	{"Slider+Phone", kSliderPhoneProfileDevices, 2},
	{"Dolly", kDollyProfileDevices, 1},
	{"Dolly+Phone", kDollyPhoneProfileDevices, 2},
};
const int kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);
