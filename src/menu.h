#pragma once

#include "buttons.h"
#include "devices/device.h"

// UI state machine:
//  - DeviceList: navigate registered devices, Ok toggles a device
//    active/inactive (connect/disconnect via BleManager), Right enters
//    Control, long-Up enters Settings.
//  - Control: fans abstract Commands out to every *active* device at
//    once. Long-Left backs out to DeviceList.
//  - Settings: currently just the OTA entry point. Long-Left backs out.
class Menu {
public:
	void begin();
	void handleButton(ButtonId id, ButtonEvent ev);
	void render();
	void updateStatusLed();

private:
	enum class Screen { DeviceList, Control, Settings };

	void renderDeviceList();
	void renderControl();
	void renderSettings();

	Screen _screen = Screen::DeviceList;
	int _selected = 0;
};

extern Menu menu;
