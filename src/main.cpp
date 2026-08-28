#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "config.h"
#include "display.h"
#include "pcf8575.h"
#include "buttons.h"
#include "battery.h"
#include "ble_manager.h"
#include "ota.h"
#include "led.h"
#include "buzzer.h"
#include "device_registry.h"
#include "menu.h"
#include "rig_store.h"
#include "settings.h"

static const ButtonId kAllButtons[] = {
	ButtonId::Up, ButtonId::Down, ButtonId::Left, ButtonId::Right, ButtonId::Ok,
};

// Set in setup() when waking from deep sleep via the center button;
// consumed (and cleared) the first time an Ok event actually shows up in
// loop() — see the wake-cause comment in setup().
static bool ignoreNextOkEvent = false;

// Deep sleep (v14 §7, stage 2 only — see config.h). Puts both status
// LEDs off first: they live on the PCF8575, which stays powered
// independently of the MCU's sleep state, so they'd otherwise keep
// burning current showing stale state through the whole sleep.
static void enterDeepSleep() {
	statusLed.setPower(false);
	statusLed.setActivity(false);
	statusLed.update();

	// The ST7789/NV3030B panel keeps showing its last frame with zero
	// drive from the MCU — without this, the screen looks fully awake the
	// whole time we're actually asleep, backlight included.
	//
	// Plain digitalWrite() only holds the pin through the regular GPIO
	// matrix, which powers down for the duration of deep sleep — same
	// issue as PIN_BTN_OK below, just for an output instead of an input.
	// The pin drifts back to its floating reset state once the digital
	// domain is off, and the backlight looks like it never turned off.
	// Route it through the RTC GPIO domain (stays powered) and latch the
	// LOW level with rtc_gpio_hold_en() so it actually sticks; released
	// again in setup() on wake, since the hold also blocks display.begin()
	// from turning the backlight back on otherwise.
#ifdef PIN_TFT_BL
	rtc_gpio_init((gpio_num_t)PIN_TFT_BL);
	rtc_gpio_set_direction((gpio_num_t)PIN_TFT_BL, RTC_GPIO_MODE_OUTPUT_ONLY);
	rtc_gpio_set_level((gpio_num_t)PIN_TFT_BL, 0);
	rtc_gpio_hold_en((gpio_num_t)PIN_TFT_BL);
#endif

	// Hand the pin to the RTC domain explicitly — plain pinMode()'s
	// pull-up (regular GPIO matrix) doesn't carry over to deep sleep. Without
	// this, the pin can float LOW right as sleep starts, and since ext0 is a
	// level trigger (not edge), that instantly wakes the chip back up —
	// looks exactly like "never actually sleeps".
	rtc_gpio_init((gpio_num_t)PIN_BTN_OK);
	rtc_gpio_set_direction((gpio_num_t)PIN_BTN_OK, RTC_GPIO_MODE_INPUT_ONLY);
	rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_OK);
	rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_OK);
	esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_OK, 0); // wake on LOW (button press)
	esp_sleep_enable_timer_wakeup((uint64_t)DAILY_CHECK_SEC * 1000000ULL);
	esp_deep_sleep_start(); // does not return
}

// Woken solely to check the battery (daily timer, not a button press) —
// skip display/BLE entirely and go straight back to sleep, so this is as
// short and low-power as possible. A critically low reading blinks the
// yellow LED a few times before sleeping again.
static void handleDailyCheckAndResleep() {
	pcf8575.begin(PCF8575_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL, PIN_PCF_INT);
	battery.begin();
	battery.update();

	if (battery.percent() <= BATT_CRITICAL_PCT) {
		for (int i = 0; i < 5; i++) {
			pcf8575.writeBit(PCF_BIT_LED_YELLOW, false); // active-LOW: on
			delay(150);
			pcf8575.writeBit(PCF_BIT_LED_YELLOW, true); // off
			delay(150);
		}
	}

	enterDeepSleep();
}

void setup() {
	Serial.begin(SERIAL_BAUD);

	esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
	if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
		handleDailyCheckAndResleep(); // never returns
	}

	// Undo the rtc_gpio_hold_en()/rtc_gpio_init() from enterDeepSleep().
	// hold_dis() alone releases the latched LOW *value*, but doesn't hand
	// the pad back to the digital GPIO matrix — it can stay routed through
	// the RTC module, which is fine for pinMode()+digitalWrite() (confirmed
	// working for PIN_BTN_OK below) but silently leaves analogWrite()'s
	// LEDC output with nowhere to go, so the backlight never comes back
	// after the first sleep. rtc_gpio_deinit() explicitly returns the pad
	// to GPIO-matrix control, which is what LEDC needs to actually drive
	// it. No-op on a fresh boot, where nothing is held yet.
#ifdef PIN_TFT_BL
	rtc_gpio_hold_dis((gpio_num_t)PIN_TFT_BL);
	rtc_gpio_deinit((gpio_num_t)PIN_TFT_BL);
#endif

	settings.begin(); // must run before display.begin(), which reads settings.brightness()
	rigStore.begin();
	deviceRegistry.begin();

	pcf8575.begin(PCF8575_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL, PIN_PCF_INT);

	buttons.begin();
	battery.begin();
	display.begin();
	statusLed.begin();
	buzzer.begin();

	NimBLEDevice::init("XIAO-Remote");
	bleManager.begin();

	// The OTA upload is handled synchronously inside WebServer::handleClient(),
	// so let it paint the progress bar itself from inside the transfer.
	ota.setProgressCallback([](uint8_t) { menu.renderOtaProgress(); });

	menu.begin();
	menu.render();

	// Woken by the center button from deep sleep: that press only wakes
	// the MCU, per v14 §7 — the actual Press/LongPress event for Ok
	// hasn't necessarily fired yet at this point (it only fires on
	// release, and the button may still be held), so this is consumed in
	// loop() the first time it actually shows up, not here.
	ignoreNextOkEvent = (wakeCause == ESP_SLEEP_WAKEUP_EXT0);
}

void loop() {
	buttons.update();
	battery.update();
	bleManager.update();
	ota.update();
	buzzer.update();
	statusLed.update();
	deviceRegistry.tick();

	uint32_t now = millis();
	static uint32_t lastActivityMs = millis(); // boot counts as activity
	static bool screenOff = false;

	// Session transitions that happen without a button press: a rig's Main
	// finishing its connection (Connecting -> Control), or its link dropping
	// (control -> lost-Main takeover, plus an auto E-Stop if mid-take).
	// Returns true when the screen needs the full redraw render() does.
	bool dirty = menu.update();
	bool woke = false;

	for (ButtonId id : kAllButtons) {
		ButtonEvent ev = buttons.poll(id);
		if (ev == ButtonEvent::None) continue;

		if (id == ButtonId::Ok && ignoreNextOkEvent) {
			ignoreNextOkEvent = false; // the deep-sleep wake press — consume silently
			continue;
		}

		lastActivityMs = now;
		if (screenOff || woke) {
			// Stage-1 wake: this press only lights the screen back up (v14 §7).
			screenOff = false;
			woke = true;
			continue;
		}

		if (settings.buttonSound()) buzzer.beep(); // tactile/audio confirmation
		menu.handleButton(id, ev);
		dirty = true;
	}
	if (woke) {
		display.setBrightness(settings.brightness());
		dirty = true;
	} else if (!screenOff && lastActivityMs != 0 && now - lastActivityMs > IDLE_SCREEN_OFF_MS) {
		screenOff = true;
		display.setBrightness(0);
	}

	// A full render() does a fillScreen() first — needed when navigating to
	// a different screen or on an OTA state transition (different widgets
	// per state), but doing that unconditionally on every tick was the
	// actual bug: this used to call menu.render() on a timer regardless of
	// whether anything had changed, and every one of those flashed the
	// whole panel blank before redrawing over it — a periodic flicker no
	// matter how long that timer was tuned to (500ms, then 3000ms). Fields
	// that legitimately tick on their own (battery%, BLE connect state,
	// slider telemetry) now go through menu.renderDynamic() instead, which
	// only touches the small regions that actually changed — see menu.cpp.
	static Ota::State lastOtaState = Ota::State::Idle;
	bool otaChanged = ota.state() != lastOtaState;
	lastOtaState = ota.state();

	menu.updateStatusLed(); // cheap; keeps LEDs live even with the screen off

	// Stage 1: screen dark -> pause all rendering, but keep BLE / menu.update()
	// running so the session catches up the moment the screen wakes.
	static const uint32_t kDynamicRenderMs = 300;
	static uint32_t lastRender = 0;
	if (!screenOff) {
		if (dirty || otaChanged) {
			menu.render();
			lastRender = now;
		} else if (now - lastRender > kDynamicRenderMs) {
			menu.renderDynamic();
			lastRender = now;
		}
	}

	// Stage 2: deep sleep after IDLE_DEEPSLEEP_MS of no button activity —
	// unless a take is recording (BLE must stay up for STOP / E-Stop).
	if (now - lastActivityMs > IDLE_DEEPSLEEP_MS && !menu.takeActive()) {
		enterDeepSleep(); // does not return
	}
}
