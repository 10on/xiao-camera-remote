#include "menu.h"
#include "display.h"
#include "ble_manager.h"
#include "ota.h"
#include "led.h"
#include "battery.h"
#include "theme.h"
#include "profile.h"
#include "settings.h"
#include "devices/dolly_device.h"
#include "devices/phone_device.h"
#include "devices/slider_device.h"
#include <string.h>

Menu menu;

// Devices available from the device list. Add more here as they're
// implemented (e.g. a second BLE peripheral for another piece of gear) —
// they also need registering with bleManager in Menu::begin(). Indices
// into this table are what Profile::deviceIndices (profile.h) refers to.
static Device *const kDevices[] = {
	&sliderDevice,
	&phoneDevice,
	&dollyDevice,
};
static const int kDeviceCount = sizeof(kDevices) / sizeof(kDevices[0]);

static void fanOut(Command cmd) {
	for (int i = 0; i < kDeviceCount; i++) {
		if (kDevices[i]->isActive()) {
			kDevices[i]->handleCommand(cmd);
		}
	}
}

// Ok on Control always combos move+record the moment a phone and a
// motion device are both active — not a per-BindingPreset opt-in (see
// profile.h's comment on why that used to require digging through
// Menu -> Profiles -> Bindings just to get the basic "press one button,
// it moves and records" behavior).
static bool comboModeActive() {
	return phoneDevice.isActive() && (sliderDevice.isActive() || dollyDevice.isActive());
}

// Activates every device listed in kProfiles[profileIndex] and
// deactivates every other registered device. Called on boot (persisted
// selection) and whenever the user applies a different profile.
static void applyProfile(int profileIndex) {
	const Profile &p = kProfiles[profileIndex];
	for (int i = 0; i < kDeviceCount; i++) {
		bool shouldBeActive = false;
		for (int j = 0; j < p.deviceCount; j++) {
			if (p.deviceIndices[j] == i) {
				shouldBeActive = true;
				break;
			}
		}
		Device *dev = kDevices[i];
		if (shouldBeActive && !dev->isActive()) dev->activate();
		else if (!shouldBeActive && dev->isActive()) dev->deactivate();
	}
}

// Whether each device has actually connected at least once since it was
// last activated — distinct from isActive()/isConnected() alone. Needed
// because a device is isActive() && !isConnected() for two very
// different reasons: it just got turned on and hasn't finished its first
// connection yet (normal, shown as the DeviceList/Control screens' own
// "connecting…" pill — not a takeover), or it *was* connected and the
// link actually dropped (the takeover's real job, per the handoff: "Fires
// on BLE link loss"). Without this, the takeover fired on every ordinary
// first connection attempt and blocked the whole UI behind "NO LINK"
// until the link happened to come up — reported as "screen is stuck
// showing just Slider, no idea how to use this".
static bool everConnected[kDeviceCount];

// First device that was connected and has since dropped, if any — the
// one the connection-lost takeover names. With one registered device
// active by default today this is at most a single hit, but stays
// generic for when more are active at once.
static Device *firstLostDevice() {
	for (int i = 0; i < kDeviceCount; i++) {
		if (!kDevices[i]->isActive()) {
			everConnected[i] = false; // next activation starts its own fresh "connecting…" window
		} else if (kDevices[i]->isConnected()) {
			everConnected[i] = true;
		}
	}
	for (int i = 0; i < kDeviceCount; i++) {
		if (everConnected[i] && kDevices[i]->isActive() && !kDevices[i]->isConnected()) return kDevices[i];
	}
	return nullptr;
}

// Comma-separated names of every other connected device, for the
// takeover's footer line (handoff's mock: "телефон, слайдер — ок").
// Static buffer since drawFooter just wants a const char*.
static const char *otherLinksFooterText() {
	static char buf[64];
	buf[0] = '\0';
	bool any = false;
	for (int i = 0; i < kDeviceCount; i++) {
		if (kDevices[i]->isConnected()) {
			if (any) strlcat(buf, ", ", sizeof(buf));
			strlcat(buf, kDevices[i]->name(), sizeof(buf));
			any = true;
		}
	}
	if (!any) return "NO OTHER LINKS";
	strlcat(buf, " OK", sizeof(buf));
	return buf;
}

// Shared connection-summary helper — used for both the header link dot and
// the top bar color on the Main screen (see docs/screen-design.md, the
// flat-bar fallback for the spec's per-state vignette).
static void summarizeLinks(bool &anyConnected, bool &anyConnecting) {
	anyConnected = false;
	anyConnecting = false;
	for (int i = 0; i < kDeviceCount; i++) {
		if (kDevices[i]->isConnected()) anyConnected = true;
		else if (kDevices[i]->isActive()) anyConnecting = true;
	}
}

// The glass has genuinely rounded active-area corners. The HTML's nominal
// 13px padding is safe in the middle of the screen but not on the first and
// last text rows, so header/footer use the measured 28px corner inset.
static constexpr int16_t kHeaderTextY = 10;
static constexpr int16_t kHeaderGlyphH = 8;
static constexpr int16_t kMainRowsTop = 68;

// Draws the compact, divider-free header from the handoff.
static void drawHeader(Adafruit_GFX &tft, const char *title) {
	tft.setFont(nullptr);
	tft.setTextSize(theme::kSizeHint);
	tft.setTextColor(theme::kTextSecondary);
	tft.setCursor(theme::kCornerSafeInset, kHeaderTextY);
	tft.print(title);
}

// Battery%/connection-dot corner of the header. Split out from drawHeader
// because both fields change on their own (battery ticks, BLE connects/
// drops) without any button press — redrawing the *whole* header (or
// worse, the whole screen) just to reflect that meant a full-screen
// fillScreen() flash every few seconds even when the user hadn't touched
// anything. This only wipes its own small corner first.
static void drawHeaderStatus(Adafruit_GFX &tft, bool anyConnected, bool anyConnecting,
	                         uint16_t forcedDotColor = 0, bool hideDot = false) {
	// Erase just this corner — widest realistic content is "100%" + gap +
	// dot, comfortably under 66px — not the whole header.
	const int16_t kStatusW = 66;
	tft.fillRect(tft.width() - kStatusW, kHeaderTextY, kStatusW, kHeaderGlyphH, theme::kBackground);

	char battStr[6];
	snprintf(battStr, sizeof(battStr), "%d%%", battery.percent());
	int16_t x1, y1;
	uint16_t w, h;
	tft.setFont(nullptr);
	tft.setTextSize(theme::kSizeHint);
	tft.getTextBounds(battStr, 0, 0, &x1, &y1, &w, &h);
	int16_t battX = tft.width() - theme::kCornerSafeInset - (int16_t)w;
	tft.setTextColor(theme::kTextPrimary);
	tft.setCursor(battX, kHeaderTextY);
	tft.print(battStr);

	if (!hideDot) {
		uint16_t dotColor = forcedDotColor ? forcedDotColor :
		                    anyConnected ? theme::kOkFill : anyConnecting ? theme::kWarnFill : theme::kTextInactive;
		int16_t dotR = 3;
		tft.fillCircle(battX - 5 - dotR, kHeaderTextY + kHeaderGlyphH / 2, dotR, dotColor);
	}
}

// Draws (or redraws) one device row's status pill. `erase` wipes a
// fixed-width box first — needed on a dynamic refresh since e.g. "READY"
// is wider than "OFF" and the old glyphs would otherwise show through;
// skipped on a full render where fillScreen() already cleared everything.
static void drawStatusPill(Adafruit_GFX &tft, Device *dev, int16_t rowY, bool erase) {
	if (erase) {
		const int16_t kPillEraseW = 60;
		const int16_t kPillEraseH = 20;
		tft.fillRect(tft.width() - theme::kPadH - kPillEraseW, rowY + 4, kPillEraseW, kPillEraseH,
		             theme::kBackground);
	}
	const char *statusText = dev->isConnected() ? "READY" : dev->isActive() ? ".." : "OFF";
	uint16_t pillFill = dev->isConnected() ? theme::kOkFill : dev->isActive() ? theme::kWarnFill : theme::kDivider;
	uint16_t pillText = dev->isConnected() ? theme::kOkText : dev->isActive() ? theme::kWarnText : theme::kTextInactive;
	theme::drawPill(tft, statusText, tft.width() - theme::kPadH, rowY + 4, pillFill, pillText);
}

// Draws the shared footer hint row, bottom-anchored.
static void drawFooter(Adafruit_GFX &tft, const char *hint, uint16_t color = theme::kTextHint) {
	tft.setFont(nullptr);
	tft.setTextSize(theme::kSizeHint);
	tft.setTextColor(color);
	int16_t x1, y1;
	uint16_t w, h;
	tft.getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
	int16_t x = (tft.width() - (int16_t)w) / 2;
	if (x < theme::kCornerSafeInset) x = theme::kCornerSafeInset;
	tft.setCursor(x, tft.height() - 18);
	tft.print(hint);
}

static const char *activeProfileName() {
	int index = settings.profileIndex();
	if (index < 0 || index >= kProfileCount) index = 0;
	return kProfiles[index].name;
}

static void drawCenteredText(Adafruit_GFX &tft, const char *text, int16_t baselineY) {
	int16_t x1, y1;
	uint16_t w, h;
	tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
	tft.setCursor((tft.width() - (int16_t)w) / 2 - x1, baselineY);
	tft.print(text);
}

static void drawThreeItemList(Adafruit_GFX &tft, const char *const *items,
	                            int count, int selected) {
	const int visible = count < 3 ? count : 3;
	int first = selected - 1;
	if (first < 0) first = 0;
	if (first > count - visible) first = count - visible;
	const int16_t itemH = 34;
	const int16_t totalH = visible * itemH + (visible - 1) * theme::kRowGap;
	int16_t y = 26 + (190 - totalH) / 2;
	for (int slot = 0; slot < visible; slot++) {
		const int index = first + slot;
		theme::drawListItem(tft, items[index], theme::kPadH, y,
		                    tft.width() - 2 * theme::kPadH, index == selected);
		y += itemH + theme::kRowGap;
	}
}

// Live-adjustable brightness screen (Settings' SettingsMode::Brightness).
// `pendingBrightness` is the unsaved 0-255 value being previewed —
// Menu::handleSettingsButton() already pushed it to the backlight via
// display.setBrightness() before this draws, so this only needs to show
// the numeric readout/bar, not touch the backlight itself.
static void renderBrightnessAdjuster(Adafruit_GFX &tft, int pendingBrightness) {
	tft.fillScreen(theme::kBackground);
	tft.setFont(nullptr);
	tft.setTextSize(theme::kSizeHint);
	tft.setTextColor(theme::kTextSecondary);
	tft.setCursor(theme::kCornerSafeInset, theme::kPadV);
	tft.print("BRIGHTNESS");

	char valStr[8];
	snprintf(valStr, sizeof(valStr), "%d", (pendingBrightness * 100) / 255);
	tft.setTextSize(theme::kSizeBigVal);
	tft.setTextColor(theme::kTextPrimary);
	int16_t x1, y1;
	uint16_t w, h;
	tft.getTextBounds(valStr, 0, 0, &x1, &y1, &w, &h);
	int16_t y = 90;
	tft.setCursor((tft.width() - (int16_t)w) / 2, y);
	tft.print(valStr);

	int16_t barY = y + (int16_t)h + 16;
	int16_t barW = tft.width() - 2 * theme::kPadH;
	int16_t barH = 8;
	tft.drawRect(theme::kPadH, barY, barW, barH, theme::kBorder);
	int16_t fillW = (barW - 2) * pendingBrightness / 255;
	tft.fillRect(theme::kPadH + 1, barY + 1, fillW, barH - 2, theme::kOkFill);

	drawFooter(tft, "L/R:ADJUST  OK:SAVE  HOLD-L:CANCEL");
}

// OTA start confirmation (Settings' SettingsMode::OtaConfirm) — the
// handoff requires a confirm step before entering OTA since it kills BLE.
static void renderOtaConfirmScreen(Adafruit_GFX &tft, int selected) {
	tft.fillScreen(theme::kBackground);
	tft.setFont(nullptr);
	tft.setTextSize(theme::kSizeHint);
	tft.setTextColor(theme::kTextSecondary);
	tft.setCursor(theme::kCornerSafeInset, theme::kPadV);
	tft.print("START OTA?");

	static const char *kItems[] = {"YES", "NO"};
	int16_t y = theme::kPadV + 20;
	for (int i = 0; i < 2; i++) {
		int16_t h = theme::drawListItem(tft, kItems[i], theme::kPadH, y, tft.width() - 2 * theme::kPadH,
		                                 i == selected);
		y += h + theme::kRowGap;
	}

	drawFooter(tft, "UP/DN  OK:CONFIRM  HOLD-L:CANCEL");
}

void Menu::begin() {
	for (int i = 0; i < kDeviceCount; i++) {
		bleManager.registerDevice(kDevices[i]);
	}
	_screen = Screen::DeviceList;
	_selected = 0;
	applyProfile(settings.profileIndex());
}

bool Menu::connectionLostTakeoverActive() const {
	// Recording keeps the timer on screen; the handoff explicitly gives it
	// priority over the full-screen lost-link takeover.
	return !_comboActive && (_screen == Screen::DeviceList || _screen == Screen::Control) &&
	       firstLostDevice() != nullptr;
}

void Menu::handleSettingsButton(ButtonId id, ButtonEvent ev) {
	// OTA is exclusive. The handoff leaves only the centre button alive.
	if (ota.state() != Ota::State::Idle) {
		if (ev == ButtonEvent::Press && id == ButtonId::Ok) {
			ota.cancel();
			_screen = Screen::DeviceList;
		}
		return;
	}

	switch (_settingsMode) {
	case SettingsMode::List:
		if (ev == ButtonEvent::Press) {
			switch (id) {
			case ButtonId::Left:
				_screen = Screen::DeviceList;
				break;
			case ButtonId::Up:
				_settingsSelected = (_settingsSelected + 2) % 3;
				break;
			case ButtonId::Down:
				_settingsSelected = (_settingsSelected + 1) % 3;
				break;
			case ButtonId::Ok:
				if (_settingsSelected == 0) {
					_profileSelected = settings.profileIndex();
					_screen = Screen::ProfileSelect;
				} else if (_settingsSelected == 1) {
					_pendingBrightness = settings.brightness();
					_settingsMode = SettingsMode::Brightness;
				} else {
					_otaConfirmSelected = 1; // default to the safe choice, NO
					_settingsMode = SettingsMode::OtaConfirm;
				}
				break;
			default:
				break;
			}
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
			_screen = Screen::DeviceList;
		}
		break;

	case SettingsMode::Brightness:
		if (ev == ButtonEvent::Press) {
			const int kStep = 16;
			const int kMin = 10; // stay visible — 0 would strand the user with no way to see the menu
			if (id == ButtonId::Left) {
				_pendingBrightness -= kStep;
				if (_pendingBrightness < kMin) _pendingBrightness = kMin;
				display.setBrightness((uint8_t)_pendingBrightness);
			} else if (id == ButtonId::Right) {
				_pendingBrightness += kStep;
				if (_pendingBrightness > 255) _pendingBrightness = 255;
				display.setBrightness((uint8_t)_pendingBrightness);
			} else if (id == ButtonId::Ok) {
				settings.setBrightness((uint8_t)_pendingBrightness);
				_settingsMode = SettingsMode::List;
			}
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
			display.setBrightness(settings.brightness()); // revert the unsaved live preview
			_settingsMode = SettingsMode::List;
		}
		break;

	case SettingsMode::OtaConfirm:
		if (ev == ButtonEvent::Press) {
			switch (id) {
			case ButtonId::Up:
			case ButtonId::Left:
				_otaConfirmSelected = 0;
				break;
			case ButtonId::Down:
			case ButtonId::Right:
				_otaConfirmSelected = 1;
				break;
			case ButtonId::Ok:
				if (_otaConfirmSelected == 0) ota.begin();
				_settingsMode = SettingsMode::List;
				break;
			default:
				break;
			}
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
			_settingsMode = SettingsMode::List;
		}
		break;
	}
}

void Menu::handleButton(ButtonId id, ButtonEvent ev) {
	if (ev == ButtonEvent::None) return;

	if (connectionLostTakeoverActive()) {
		// The takeover replaces the screen entirely — only Ok does
		// anything (cosmetically restarts the "reconnecting" message;
		// BleManager is already retrying regardless, see ble_manager.h).
		// Everything else is swallowed so it can't silently toggle/move a
		// device hidden underneath the overlay.
		if (ev == ButtonEvent::Press && id == ButtonId::Ok) {
			_lostSinceMs = millis();
		}
		return;
	}

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
				_comboActive = false; // start each visit assuming nothing's running yet
				break;
			default:
				break;
			}
		} else if (ev == ButtonEvent::LongPress && (id == ButtonId::Up || id == ButtonId::Ok)) {
			_screen = Screen::Settings;
			_settingsMode = SettingsMode::List;
		}
		break;

	case Screen::Control: {
		const BindingPreset &preset = kBindingPresets[settings.bindingPresetIndex()];
		bool combo = comboModeActive();
		if (ev == ButtonEvent::Press) {
			if (id == ButtonId::Ok && combo) {
				_comboActive = !_comboActive;
				if (_comboActive) {
					_recordStartedAtMs = millis();
					fanOut(Command::MoveForward);
					fanOut(Command::Record);
				} else {
					_recordStartedAtMs = 0;
					fanOut(Command::StopMove);
					fanOut(Command::StopRecord);
				}
			} else {
				fanOut(preset.onPress[static_cast<int>(id)]);
			}
		} else if (ev == ButtonEvent::LongPress) {
			if (id == ButtonId::Left) {
				if (_comboActive) {
					fanOut(Command::StopMove);
					fanOut(Command::StopRecord);
				}
				_screen = Screen::DeviceList;
				_comboActive = false;
				_recordStartedAtMs = 0;
			} else if (id == ButtonId::Ok && combo) {
				// Unconditional kill switch regardless of what _comboActive
				// currently believes — that's just a locally-tracked flag,
				// not proof either device is actually still doing what we
				// last told it to (e.g. a command got dropped over BLE).
				_comboActive = false;
				_recordStartedAtMs = 0;
				fanOut(Command::EmergencyStop);
				fanOut(Command::StopRecord);
			} else {
				fanOut(preset.onLongPress[static_cast<int>(id)]);
			}
		}
		break;
	}

	case Screen::Settings:
		handleSettingsButton(id, ev);
		break;

	case Screen::ProfileSelect:
		if (ev == ButtonEvent::Press) {
			switch (id) {
			case ButtonId::Left:
				_screen = Screen::Settings;
				_settingsMode = SettingsMode::List;
				break;
			case ButtonId::Up:
				_profileSelected = (_profileSelected + kProfileCount - 1) % kProfileCount;
				break;
			case ButtonId::Down:
				_profileSelected = (_profileSelected + 1) % kProfileCount;
				break;
			case ButtonId::Ok:
				settings.setProfileIndex(_profileSelected);
				applyProfile(_profileSelected);
				_screen = Screen::Settings;
				_settingsMode = SettingsMode::List;
				break;
			case ButtonId::Right:
				_bindingSelected = settings.bindingPresetIndex();
				_screen = Screen::BindingPresets;
				break;
			default:
				break;
			}
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
			_screen = Screen::Settings;
			_settingsMode = SettingsMode::List;
		}
		break;

	case Screen::BindingPresets:
		if (ev == ButtonEvent::Press) {
			switch (id) {
			case ButtonId::Left:
				_screen = Screen::ProfileSelect;
				break;
			case ButtonId::Up:
				_bindingSelected = (_bindingSelected + kBindingPresetCount - 1) % kBindingPresetCount;
				break;
			case ButtonId::Down:
				_bindingSelected = (_bindingSelected + 1) % kBindingPresetCount;
				break;
			case ButtonId::Ok:
				settings.setBindingPresetIndex(_bindingSelected);
				_screen = Screen::ProfileSelect;
				break;
			default:
				break;
			}
		} else if (ev == ButtonEvent::LongPress && id == ButtonId::Left) {
			_screen = Screen::ProfileSelect;
		}
		break;
	}
}

// Connection-lost takeover overrides whichever screen would normally
// render (see connectionLostTakeoverActive()), but never `_screen`
// itself — so it "auto-dismisses" for free the moment the device
// reconnects, resuming exactly the screen the user was on, with nothing
// extra to track.
// Both render() and renderDynamic() end with display.flush() — every
// draw call above only touches the offscreen canvas (see display.h);
// nothing reaches the physical panel until the composited frame is
// pushed there in one shot, which is what actually fixes the tearing/
// jitter that immediate small draws straight to the panel used to cause.
void Menu::render() {
	bool lost = connectionLostTakeoverActive();
	if (lost) {
		if (_lostSinceMs == 0) _lostSinceMs = millis();
		renderConnectionLost();
	} else {
		_lostSinceMs = 0;
		switch (_screen) {
		case Screen::DeviceList: renderDeviceList(); break;
		case Screen::Control: _comboActive ? renderRecording() : renderControl(); break;
		case Screen::Settings: renderSettings(); break;
		case Screen::ProfileSelect: renderProfileSelect(); break;
		case Screen::BindingPresets: renderBindingPresets(); break;
		}
	}
	display.flush();
}

void Menu::renderDynamic() {
	bool lost = connectionLostTakeoverActive();
	if (lost != (_lostSinceMs != 0)) {
		// Transition detected outside a button press (BLE dropped or
		// recovered on its own, between ticks) — needs a full redraw
		// either way: entering the takeover replaces the whole screen,
		// leaving it must restore whatever fillScreen()'d content the
		// takeover was covering. render() handles both, and already
		// flushes — don't do it twice.
		render();
		return;
	}
	if (lost) {
		renderConnectionLostDynamic();
	} else {
		switch (_screen) {
		case Screen::DeviceList: renderDeviceListDynamic(); break;
		case Screen::Control: _comboActive ? renderRecordingDynamic() : renderControlDynamic(); break;
		case Screen::Settings:
		case Screen::ProfileSelect:
		case Screen::BindingPresets:
			break; // everything on these only changes on a button press (dirty), already a full render
		}
	}
	display.flush();
}

// "Main" screen from docs/screen-design.md: one row per registered device
// with an identity-colored icon chip + name + a state-colored status pill.
void Menu::renderDeviceList() {
	Adafruit_GFX &tft = display.canvas();
	tft.fillScreen(theme::kBackground);

	bool anyConnected, anyConnecting;
	summarizeLinks(anyConnected, anyConnecting);
	uint16_t barColor = anyConnecting ? theme::kWarnFill : theme::kPhoneFill;
	theme::drawTopBar(tft, barColor);

	drawHeader(tft, activeProfileName());
	drawHeaderStatus(tft, anyConnected, anyConnecting);

	const int16_t rowH = theme::kIconChip;
	const int16_t rowGap = 9;
	for (int i = 0; i < kDeviceCount; i++) {
		Device *dev = kDevices[i];
		int16_t rowY = kMainRowsTop + i * (rowH + rowGap);

		if (_screen == Screen::DeviceList && i == _selected) {
			tft.fillRect(0, rowY, theme::kAccentBarWidth, rowH, dev->identityColor565());
		}

		int16_t chipX = theme::kPadH + theme::kAccentBarWidth + 4;
		theme::drawIconChip(tft, chipX, rowY, dev->abbrev(), dev->identityColor565(), dev->identityTextColor565());

		tft.setFont(&FreeSansBold9pt7b);
		tft.setTextSize(1);
		tft.setTextColor(theme::kTextPrimary);
		int16_t x1, y1;
		uint16_t w, h;
		tft.getTextBounds(dev->name(), 0, 0, &x1, &y1, &w, &h);
		tft.setCursor(chipX + theme::kIconChip + 9, rowY + (rowH - (int16_t)h) / 2 - y1);
		tft.print(dev->name());
		tft.setFont(nullptr);

		drawStatusPill(tft, dev, rowY, /*erase=*/false);
	}

	drawFooter(tft, _screen == Screen::Control ? "OK:REC  D-PAD:MOTION  HOLD-L:BACK" :
	                                           "OK:DEVICE  RIGHT:CTRL  HOLD-UP:MENU");
}

// Cheap refresh for the DeviceList screen's fields that can change without
// a button press (battery ticking, BLE connecting/dropping) — no
// fillScreen(), just the small regions that actually need new pixels. See
// drawHeaderStatus/drawStatusPill for what each one erases before redraw.
void Menu::renderDeviceListDynamic() {
	Adafruit_GFX &tft = display.canvas();

	bool anyConnected, anyConnecting;
	summarizeLinks(anyConnected, anyConnecting);
	drawHeaderStatus(tft, anyConnected, anyConnecting);

	const int16_t rowH = theme::kIconChip;
	const int16_t rowGap = 9;
	for (int i = 0; i < kDeviceCount; i++) {
		int16_t rowY = kMainRowsTop + i * (rowH + rowGap);
		drawStatusPill(tft, kDevices[i], rowY, /*erase=*/true);
	}
}

void Menu::renderRecording() {
	Adafruit_GFX &tft = display.canvas();
	tft.fillScreen(theme::kBackground);
	theme::drawTopBar(tft, theme::kRecordFill);
	tft.drawRect(0, 0, tft.width(), tft.height(), theme::kRecordFill);

	bool anyConnected, anyConnecting;
	summarizeLinks(anyConnected, anyConnecting);
	drawHeader(tft, activeProfileName());
	drawHeaderStatus(tft, anyConnected, anyConnecting, theme::kRecordFill);

	const char *recordBadge = "REC";
	tft.setFont(nullptr);
	tft.setTextSize(theme::kSizeHint);
	int16_t x1, y1;
	uint16_t w, h;
	tft.getTextBounds(recordBadge, 0, 0, &x1, &y1, &w, &h);
	theme::drawPill(tft, recordBadge, (tft.width() + (int16_t)w + 18) / 2,
	                48, theme::kRecordFill, theme::kRecordText);

	uint32_t elapsed = _recordStartedAtMs ? (millis() - _recordStartedAtMs) / 1000 : 0;
	char timer[12];
	if (elapsed < 3600) {
		snprintf(timer, sizeof(timer), "%02lu:%02lu",
		         static_cast<unsigned long>(elapsed / 60),
		         static_cast<unsigned long>(elapsed % 60));
		tft.setFont(&FreeMonoBold24pt7b);
	} else {
		snprintf(timer, sizeof(timer), "%lu:%02lu:%02lu",
		         static_cast<unsigned long>(elapsed / 3600),
		         static_cast<unsigned long>((elapsed / 60) % 60),
		         static_cast<unsigned long>(elapsed % 60));
		tft.setFont(&FreeMonoBold18pt7b);
	}
	tft.setTextSize(1);
	tft.setTextColor(theme::kRecordText);
	drawCenteredText(tft, timer, 125);
	tft.setFont(nullptr);

	const bool motionActive = sliderDevice.isActive() || dollyDevice.isActive();
	if (motionActive) {
		const char *motionBadge = "MOTION";
		tft.setTextSize(theme::kSizeHint);
		tft.getTextBounds(motionBadge, 0, 0, &x1, &y1, &w, &h);
		theme::drawPill(tft, motionBadge, (tft.width() + (int16_t)w + 18) / 2,
		                151, theme::kOkFill, theme::kOkText);
	}

	drawFooter(tft, "OK - STOP", theme::kRecordFill);
}

void Menu::renderRecordingDynamic() {
	// The framebuffer means a complete recomposition is still presented as
	// one tear-free frame, and keeps the timer, battery and link state atomic.
	renderRecording();
}

void Menu::renderControl() {
	renderDeviceList();
}

// Cheap refresh for Control: battery/link corner plus each active device's
// telemetry line (position/speed tick continuously while moving). Which
// devices are active can't change while this screen is showing — that's
// only ever toggled from DeviceList — so the same devices/line count as
// the last full render() still apply; only their contents need updating.
// Each line is erased first since printf'd numbers can shrink (e.g. digit
// count dropping) and leave stale glyphs behind otherwise.
void Menu::renderControlDynamic() {
	renderDeviceListDynamic();
}

// Full-screen takeover for a lost link (docs/design's "Main / connection
// lost" screen #3) — see connectionLostTakeoverActive()/render() for when
// this replaces the normal DeviceList/Control content.
void Menu::renderConnectionLost() {
	Adafruit_GFX &tft = display.canvas();
	tft.fillScreen(theme::kBackground);
	theme::drawTopBar(tft, theme::kWarnFill);
	tft.drawRect(0, 0, tft.width(), tft.height(), theme::kWarnFill);
	drawHeader(tft, activeProfileName());
	drawHeaderStatus(tft, false, false, 0, true);

	Device *lost = firstLostDevice();
	if (!lost) return; // render() only calls this when connectionLostTakeoverActive()

	const int16_t chipSize = 32;
	int16_t y = 54;
	int16_t chipX = (tft.width() - chipSize) / 2;
	theme::drawIconChip(tft, chipX, y, lost->abbrev(), lost->identityColor565(),
	                    lost->identityTextColor565(), chipSize, 9);
	y += chipSize + 9;

	int16_t x1, y1;
	uint16_t w, h;
	tft.setFont(&FreeSansBold12pt7b);
	tft.setTextSize(1);
	tft.setTextColor(theme::kTextPrimary);
	tft.getTextBounds(lost->name(), 0, 0, &x1, &y1, &w, &h);
	tft.setCursor((tft.width() - (int16_t)w) / 2 - x1, y - y1);
	tft.print(lost->name());
	y += (int16_t)h + 7;

	tft.setFont(nullptr);
	tft.setTextSize(theme::kSizeHint);
	const char *badge = "NO LINK";
	tft.getTextBounds(badge, 0, 0, &x1, &y1, &w, &h);
	int16_t padX = 9, padY = 3;
	int16_t pillW = (int16_t)w + padX * 2, pillH = (int16_t)h + padY * 2;
	int16_t pillX = (tft.width() - pillW) / 2;
	tft.fillRoundRect(pillX, y, pillW, pillH, theme::kPillRadiusY, theme::kWarnFill);
	tft.setTextColor(theme::kWarnText);
	tft.setCursor(pillX + padX, y + padY);
	tft.print(badge);
	y += pillH + 7;

	bool retryFailed = (millis() - _lostSinceMs) > 30000;
	const char *msg = retryFailed ? "FAILED - OK:RETRY" : "RECONNECTING...";
	tft.setTextColor(theme::kTextHint);
	tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
	tft.setCursor((tft.width() - (int16_t)w) / 2, y);
	tft.print(msg);

	drawFooter(tft, otherLinksFooterText());
}

// Cheap refresh while the takeover is already showing: battery/link
// corner plus the reconnect-status line, which is the only thing that
// changes tick to tick (the 30s "failed" message swap). Recomputes the
// same y the full render used rather than caching it, mirroring how
// renderControlDynamic() re-derives its line positions.
void Menu::renderConnectionLostDynamic() {
	renderConnectionLost();
}

// Design's "Profile select" screen — picks which Profile (profile.h) is
// active. Right drills into BindingPresets for the highlighted profile.
void Menu::renderProfileSelect() {
	Adafruit_GFX &tft = display.canvas();
	tft.fillScreen(theme::kBackground);

	tft.setTextSize(theme::kSizeHint);
	tft.setTextColor(theme::kTextSecondary);
	tft.setCursor(theme::kCornerSafeInset, theme::kPadV);
	tft.print("PROFILES");

	const int visible = kProfileCount < 3 ? kProfileCount : 3;
	int first = _profileSelected - 1;
	if (first < 0) first = 0;
	if (first > kProfileCount - visible) first = kProfileCount - visible;
	const int16_t itemH = 34;
	int16_t y = 26 + (190 - (visible * itemH + (visible - 1) * theme::kRowGap)) / 2;
	for (int slot = 0; slot < visible; slot++) {
		int i = first + slot;
		theme::drawListItem(tft, kProfiles[i].name, theme::kPadH, y,
		                    tft.width() - 2 * theme::kPadH, i == _profileSelected);
		y += itemH + theme::kRowGap;
	}

	drawFooter(tft, "UP/DN:SELECT  OK:OPEN  LEFT:BACK");
}

// Design's "Binding presets" screen — picks the D-pad->Command mapping
// Control uses (see profile.h's BindingPreset and the copy-deviation note
// in the plan this was built from: real, testable presets today rather
// than the mock's device-set-flavored sample names, which would be
// no-ops with only one registered Device).
void Menu::renderBindingPresets() {
	Adafruit_GFX &tft = display.canvas();
	tft.fillScreen(theme::kBackground);

	tft.setTextSize(theme::kSizeHint);
	tft.setTextColor(theme::kTextSecondary);
	tft.setCursor(theme::kCornerSafeInset, theme::kPadV);
	tft.print("BINDINGS");

	const char *items[3] = {
		kBindingPresets[0].name,
		kBindingPresets[1].name,
		kBindingPresets[2].name,
	};
	drawThreeItemList(tft, items, kBindingPresetCount, _bindingSelected);

	drawFooter(tft, "UP/DN:PRESET  OK:APPLY  LEFT:BACK");
}

// Three sub-states: the 3-item list, the inline brightness adjuster, and
// the OTA confirm prompt (see SettingsMode) — plus the pre-existing
// OTA-active passthrough (design's screen #7), unchanged.
void Menu::renderSettings() {
	Adafruit_GFX &tft = display.canvas();

	if (ota.state() != Ota::State::Idle) {
		// OTA active — design's screen #7 ("OTA mode, exclusive").
		tft.fillScreen(theme::kBackground);
		tft.drawRect(0, 0, tft.width(), tft.height(), theme::kWarnFill);
		tft.fillRoundRect(theme::kCornerSafeInset, tft.height() - 5,
		                  tft.width() - 2 * theme::kCornerSafeInset, 3, 2, theme::kWarnFill);

		tft.setFont(nullptr);
		tft.setTextSize(theme::kSizeHint);
		tft.setTextColor(theme::kTextHint);
		tft.setCursor(theme::kCornerSafeInset, kHeaderTextY);
		tft.print("BLE: OFF");

		const char *wifiLabel = "WIFI: ON";
		int16_t x1, y1;
		uint16_t w, h;
		tft.getTextBounds(wifiLabel, 0, 0, &x1, &y1, &w, &h);
		tft.setTextColor(theme::kWarnFill);
		tft.setCursor(tft.width() - theme::kCornerSafeInset - (int16_t)w, kHeaderTextY);
		tft.print(wifiLabel);

		int16_t contentY = 81;
		tft.setTextSize(theme::kSizeBody);
		tft.setTextColor(theme::kTextPrimary);
		drawCenteredText(tft, "OTA MODE", contentY);
		contentY += 16;

		switch (ota.state()) {
		case Ota::State::Connecting:
			tft.setTextSize(theme::kSizeHint);
			tft.setTextColor(theme::kTextSecondary);
			drawCenteredText(tft, "Connecting WiFi...", contentY + 20);
			break;
		case Ota::State::WaitingForUpload:
		case Ota::State::Uploading:
			tft.setFont(&FreeMonoBold18pt7b);
			tft.setTextSize(1);
			tft.setTextColor(theme::kWarnFill);
			drawCenteredText(tft, ota.ip(), contentY + 34);
			contentY += 48;
			tft.setFont(nullptr);
			tft.setTextSize(theme::kSizeHint);
			tft.setTextColor(theme::kTextSecondary);
			drawCenteredText(tft,
			                 ota.state() == Ota::State::Uploading ? "Uploading..." : "Waiting for connection...",
			                 contentY);
			break;
		case Ota::State::Failed:
			tft.setTextSize(theme::kSizeHint);
			tft.setTextColor(theme::kWarnFill);
			drawCenteredText(tft, "Failed / timeout", contentY + 24);
			break;
		default:
			break;
		}

		// Amber "functions unavailable" block, spec's own attention element —
		// anchored just above the footer.
		int16_t blockY = tft.height() - 40;
		tft.fillRoundRect(theme::kPadH, blockY, tft.width() - 2 * theme::kPadH, 18, 6, theme::kWarnFill);
		tft.setTextSize(theme::kSizeHint);
		tft.setTextColor(theme::kWarnText);
		tft.setCursor(theme::kPadH + 6, blockY + 5);
		tft.print("FUNCTIONS DISABLED");

		drawFooter(tft, "OK - EXIT");
		return;
	}

	if (_settingsMode == SettingsMode::Brightness) {
		renderBrightnessAdjuster(tft, _pendingBrightness);
		return;
	}
	if (_settingsMode == SettingsMode::OtaConfirm) {
		renderOtaConfirmScreen(tft, _otaConfirmSelected);
		return;
	}

	// List mode — the design's "МЕНЮ" screen (Latin placeholder copy, see
	// docs/screen-design.md's Cyrillic-text note).
	tft.fillScreen(theme::kBackground);
	tft.setTextSize(theme::kSizeHint);
	tft.setTextColor(theme::kTextSecondary);
	tft.setCursor(theme::kCornerSafeInset, theme::kPadV);
	tft.print("MENU");

	static const char *kItems[] = {"PROFILES", "BRIGHTNESS", "OTA UPDATE"};
	drawThreeItemList(tft, kItems, 3, _settingsSelected);
	const int16_t trackX = tft.width() - theme::kPadH - 12 - 56;
	const int16_t trackY = 104 + 15;
	tft.fillRoundRect(trackX, trackY, 56, 4, 2, theme::kDivider);
	int16_t trackFill = 56 * settings.brightness() / 255;
	tft.fillRoundRect(trackX, trackY, trackFill, 4, 2,
	                  _settingsSelected == 1 ? theme::kOkText : theme::kTextInactive);

	drawFooter(tft, "UP/DN  OK:OPEN  LEFT:BACK");
}

void Menu::updateStatusLed() {
	// Blue (activity/workflow): solid once something is actually up,
	// blinking while still connecting. OTA takes priority — it's a
	// distinct mode, not the normal device state, and worth flagging
	// clearly since WiFi disrupts BLE.
	if (ota.state() == Ota::State::WaitingForUpload || ota.state() == Ota::State::Uploading) {
		statusLed.setActivity(true, false);
	} else {
		bool anyConnected = false;
		bool anyConnecting = false;
		for (int i = 0; i < kDeviceCount; i++) {
			if (kDevices[i]->isConnected()) anyConnected = true;
			else if (kDevices[i]->isActive()) anyConnecting = true;
		}
		statusLed.setActivity(anyConnected || anyConnecting, !anyConnected && anyConnecting);
	}

	// Yellow (power): requirements v10 §5 ties this to charge state (solid
	// = on USB, blinking = discharging low), but there's no charge-detect
	// GPIO wired up yet — approximated from battery percent until that
	// gap is closed.
	uint8_t pct = battery.percent();
	statusLed.setPower(pct <= 20, pct <= 10);
}
