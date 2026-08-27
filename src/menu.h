#pragma once

#include "buttons.h"
#include "devices/device.h"

// UI state machine:
//  - DeviceList: navigate registered devices, Ok toggles a device
//    active/inactive (connect/disconnect via BleManager), Right enters
//    Control, long-Up enters Settings.
//  - Control: fans abstract Commands out to every *active* device at
//    once, via the currently selected BindingPreset (see profile.h).
//    Long-Left backs out to DeviceList.
//  - Settings ("МЕНЮ"): a 3-item list (Профили/Яркость/OTA) plus two
//    inline sub-states (see SettingsMode) that don't warrant their own
//    Screen — a brightness adjuster and an OTA start confirmation.
//    Long-Left backs out (or cancels the current sub-state first).
//  - ProfileSelect: pick which Profile is active (see profile.h). Right
//    drills into BindingPresets for the highlighted profile, mirroring
//    DeviceList's Right-into-Control. Long-Left backs out to Settings.
//  - BindingPresets: pick the D-pad->Command mapping Control uses. Ok
//    applies and persists it. Long-Left backs out to ProfileSelect.
//
// Independently of the above, a connection-lost takeover can override
// DeviceList/Control's rendering (not their input handling or `_screen`
// itself) — see connectionLostTakeoverActive() in menu.cpp.
class Menu {
public:
	void begin();
	void handleButton(ButtonId id, ButtonEvent ev);
	void render();        // full redraw incl. fillScreen() — call on nav/state changes only
	void renderDynamic(); // cheap in-place refresh of fields that tick without a button
	void updateStatusLed();

private:
	enum class Screen { DeviceList, Control, Settings, ProfileSelect, BindingPresets };
	enum class SettingsMode { List, Brightness, OtaConfirm };

	void renderDeviceList();
	void renderControl();
	void renderSettings();
	void renderProfileSelect();
	void renderBindingPresets();
	void renderConnectionLost();
	void renderRecording();

	void renderDeviceListDynamic();
	void renderControlDynamic();
	void renderConnectionLostDynamic();
	void renderRecordingDynamic();

	bool connectionLostTakeoverActive() const;
	void handleSettingsButton(ButtonId id, ButtonEvent ev);

	Screen _screen = Screen::DeviceList;
	int _selected = 0; // DeviceList cursor

	SettingsMode _settingsMode = SettingsMode::List;
	int _settingsSelected = 0;   // cursor in the 3-item Settings list
	int _pendingBrightness = 0;  // live (unsaved) value while SettingsMode::Brightness
	int _otaConfirmSelected = 1; // 0=YES, 1=NO — defaults to the safe choice

	int _profileSelected = 0; // cursor in ProfileSelect
	int _bindingSelected = 0; // cursor in BindingPresets
	bool _comboActive = false; // locally tracked start/stop state for combo bindings
	uint32_t _recordStartedAtMs = 0;

	// First-lost timestamp for the currently-shown takeover, so it can
	// swap to "не удалось / RETRY" after 30s — see hardware-spec.md §3.
	// Reset to 0 whenever no device is lost, so a *new* loss restarts the
	// timer instead of inheriting a stale one.
	uint32_t _lostSinceMs = 0;
};

extern Menu menu;
