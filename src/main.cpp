#include <Arduino.h>
#include <NimBLEDevice.h>

#include "config.h"
#include "display.h"
#include "buttons.h"
#include "battery.h"
#include "ble_manager.h"
#include "ota.h"
#include "led.h"
#include "buzzer.h"
#include "menu.h"

static const ButtonId kAllButtons[] = {
	ButtonId::Up, ButtonId::Down, ButtonId::Left, ButtonId::Right, ButtonId::Ok,
};

void setup() {
	Serial.begin(SERIAL_BAUD);

	buttons.begin();
	battery.begin();
	display.begin();
	statusLed.begin();
	buzzer.begin();

	NimBLEDevice::init("XIAO-Remote");
	bleManager.begin();

	menu.begin();
	menu.render();
}

void loop() {
	buttons.update();
	battery.update();
	bleManager.update();
	ota.update();
	buzzer.update();

	bool dirty = false;
	for (ButtonId id : kAllButtons) {
		ButtonEvent ev = buttons.poll(id);
		if (ev != ButtonEvent::None) {
			buzzer.beep(); // tactile/audio confirmation the press registered
			menu.handleButton(id, ev);
			dirty = true;
		}
	}

	static uint32_t lastRender = 0;
	uint32_t now = millis();
	if (dirty || (now - lastRender) > 500) {
		menu.updateStatusLed();
		menu.render();
		lastRender = now;
	}
}
