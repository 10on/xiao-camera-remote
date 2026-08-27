#include "menu.h"
#include "display.h"
#include "ble_manager.h"
#include "ota.h"
#include "led.h"
#include "devices/slider_device.h"

Menu menu;

// Devices available from the device list. Add more here as they're
// implemented (e.g. a second BLE peripheral for another piece of gear) —
// they also need registering with bleManager in Menu::begin().
static Device *const kDevices[] = {
	&sliderDevice,
};
static const int kDeviceCount = sizeof(kDevices) / sizeof(kDevices[0]);

static void fanOut(Command cmd) {
	for (int i = 0; i < kDeviceCount; i++) {
		if (kDevices[i]->isActive()) {
			kDevices[i]->handleCommand(cmd);
		}
	}
}

void Menu::begin() {
	for (int i = 0; i < kDeviceCount; i++) {
		bleManager.registerDevice(kDevices[i]);
	}
	_screen = Screen::DeviceList;
	_selected = 0;
}

void Menu::handleButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::None) return;

	switch (_screen) {
	case Screen::DeviceList:
		if (ev == ButtonEvent::Press) {
			switch (id) {
			case ButtonId::Up:
				_selected = (_selected + kDeviceCount - 1) % kDeviceCount;
				break;
			case ButtonId::Down:
				_selected = (_selected + 1) % kDeviceCount;
				break;
			case ButtonId::Ok: {
				Device *dev = kDevices[_selected];
				if (dev->isActive()) dev->deactivate();
				else dev->activate();
				break;
			}
			case ButtonId::Right:
				_screen = Screen::Control;
				break;
			default:
				break;
			}
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Up) {
			_screen = Screen::Settings;
		}
		break;

	case Screen::Control:
		if (ev == ButtonEvent::Press) {
			switch (id) {
			case ButtonId::Right: fanOut(Command::MoveForward); break;
			case ButtonId::Left: fanOut(Command::MoveBackward); break;
			case ButtonId::Up: fanOut(Command::SpeedUp); break;
			case ButtonId::Down: fanOut(Command::SpeedDown); break;
			case ButtonId::Ok: fanOut(Command::StopMove); break;
			default: break;
			}
		} else if (ev == ButtonEvent::LongPress) {
			if (id == ButtonId::Ok) fanOut(Command::Home);
			else if (id == ButtonId::Left) _screen = Screen::DeviceList;
		}
		break;

	case Screen::Settings:
		if (ev == ButtonEvent::Press && id == ButtonId::Ok) {
			ota.begin();
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
			ota.cancel();
			_screen = Screen::DeviceList;
		}
		break;
	}
}

void Menu::render() {
	switch (_screen) {
	case Screen::DeviceList: renderDeviceList(); break;
	case Screen::Control: renderControl(); break;
	case Screen::Settings: renderSettings(); break;
	}
}

void Menu::renderDeviceList() {
	Adafruit_ST7789 &tft = display.tft();
	tft.fillScreen(0x0000);
	tft.setTextColor(0xFFFF);
	tft.setTextSize(2);

	tft.setCursor(10, 10);
	tft.print("Devices:");

	for (int i = 0; i < kDeviceCount; i++) {
		Device *dev = kDevices[i];
		tft.setCursor(20, 40 + i * 30);
		tft.print(i == _selected ? "> " : "  ");
		tft.print(dev->isActive() ? (dev->isConnected() ? "[x] " : "[.] ") : "[ ] ");
		tft.print(dev->name());
	}

	tft.setCursor(10, tft.height() - 20);
	tft.setTextSize(1);
	tft.print("Ok:toggle Right:control  long-Up:settings");
}

void Menu::renderControl() {
	Adafruit_ST7789 &tft = display.tft();
	tft.fillScreen(0x0000);
	tft.setTextColor(0xFFFF);
	tft.setTextSize(2);

	tft.setCursor(10, 10);
	tft.print("Control:");

	int line = 0;
	bool any = false;
	for (int i = 0; i < kDeviceCount; i++) {
		if (!kDevices[i]->isActive()) continue;
		any = true;
		kDevices[i]->renderStatusLine(tft, 40 + line * 30);
		line++;
	}
	if (!any) {
		tft.setCursor(10, 40);
		tft.print("No active devices");
	}

	tft.setCursor(10, tft.height() - 20);
	tft.setTextSize(1);
	tft.print("long-Left:back  Ok:stop  long-Ok:home");
}

void Menu::renderSettings() {
	Adafruit_ST7789 &tft = display.tft();
	tft.fillScreen(0x0000);
	tft.setTextColor(0xFFFF);
	tft.setTextSize(2);

	tft.setCursor(10, 10);
	tft.print("Settings:");

	tft.setCursor(20, 40);
	tft.print("Ok: start WiFi OTA");

	tft.setCursor(20, 70);
	switch (ota.state()) {
	case Ota::State::Idle:
		tft.print("OTA: idle");
		break;
	case Ota::State::Connecting:
		tft.print("OTA: connecting WiFi...");
		break;
	case Ota::State::WaitingForUpload:
		tft.setTextSize(1);
		tft.printf("OTA: waiting, ip %s", ota.ip());
		tft.setCursor(20, 90);
		tft.print("upload within 60s or it cancels");
		tft.setTextSize(2);
		break;
	case Ota::State::Uploading:
		tft.print("OTA: uploading...");
		break;
	case Ota::State::Failed:
		tft.print("OTA: failed/timeout");
		break;
	}

	tft.setCursor(10, tft.height() - 20);
	tft.setTextSize(1);
	tft.print("long-Left:back (cancels OTA)");
}

void Menu::updateStatusLed() {
	// OTA takes priority — it's a distinct mode, not the normal device
	// state, and worth flagging clearly since WiFi disrupts BLE.
	if (ota.state() == Ota::State::WaitingForUpload || ota.state() == Ota::State::Uploading) {
		statusLed.set(0, 0, 255); // blue: OTA in progress
		return;
	}

	bool anyConnected = false;
	bool anyConnecting = false;
	for (int i = 0; i < kDeviceCount; i++) {
		if (kDevices[i]->isConnected()) anyConnected = true;
		else if (kDevices[i]->isActive()) anyConnecting = true;
	}

	if (anyConnected) statusLed.set(0, 255, 0);       // green: at least one device up
	else if (anyConnecting) statusLed.set(255, 120, 0); // amber: waiting to connect
	else statusLed.off();                              // nothing active
}
