#include "menu.h"

#include <Arduino.h>
#include <string.h>

#include "menu_internal.h"
#include "battery.h"
#include "device_registry.h"
#include "devices/phone_device.h"
#include "display.h"
#include "ota.h"
#include "settings.h"
#include "theme.h"

// Layout follows docs/design/ux-redesign-mock/new-model.dc.html (280x240
// canvas, screen numbers in comments). Copy is English placeholder.

namespace {

Adafruit_GFX &C() { return display.canvas(); }

// ---------------------------------------------------------------------------
// Text layer — the one place font baselines are reasoned about.
//
// Every render*() draws text through text()/centerText()/pill(). They take
// the vertical CENTRE line `cy` (not a baseline, not a top) and handle the
// FreeSans/FreeMono-vs-builtin origin difference internally, so callers just
// say "centre this on the middle of that box". No magic "+N" offsets.
// ---------------------------------------------------------------------------

enum HAlign { AlignL, AlignC, AlignR };

void text(int16_t x, int16_t cy, const char *s, uint16_t color, HAlign a = AlignL,
          const GFXfont *font = nullptr, uint8_t size = 1) {
	Adafruit_GFX &t = C();
	t.setFont(font);
	t.setTextSize(size);
	t.setTextColor(color);
	int16_t x1, y1;
	uint16_t w, h;
	t.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
	int16_t cx = x - x1;
	if (a == AlignC) cx -= (int16_t)w / 2;
	else if (a == AlignR) cx -= (int16_t)w;
	t.setCursor(cx, cy - y1 - (int16_t)h / 2);
	t.print(s);
	t.setFont(nullptr);
}

void centerText(const char *s, int16_t cy, uint16_t color, const GFXfont *font = nullptr,
                uint8_t size = 1) {
	text(C().width() / 2, cy, s, color, AlignC, font, size);
}

// Status pill, right edge at `rightX`, centred vertically on `cy`.
int16_t pill(int16_t rightX, int16_t cy, const char *s, uint16_t fill, uint16_t fg) {
	Adafruit_GFX &t = C();
	t.setFont(nullptr);
	t.setTextSize(1);
	int16_t x1, y1;
	uint16_t w, h;
	t.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
	return theme::drawPill(t, s, rightX, cy - ((int16_t)h + 6) / 2, fill, fg);
}

void anyLinks(bool &connected, bool &connecting) {
	connected = connecting = false;
	for (int i = 0; i < deviceCount(); i++) {
		if (deviceAt(i)->isConnected()) connected = true;
		else if (deviceAt(i)->isActive()) connecting = true;
	}
}

const int16_t kHdrMid = 13; // vertical centre of the header text row

void chrome(const char *left, const char *right, uint16_t barColor, bool showBar = true) {
	if (showBar) theme::drawTopBar(C(), barColor);
	text(theme::kCornerSafeInset, kHdrMid, left, theme::kTextSecondary);
	if (right)
		text(C().width() - theme::kCornerSafeInset, kHdrMid, right, theme::kTextInactive, AlignR);
}

void chromeBattery(const char *left, uint16_t barColor, const char *extraRight = nullptr,
                   uint16_t extraColor = 0) {
	Adafruit_GFX &t = C();
	theme::drawTopBar(t, barColor);
	text(theme::kCornerSafeInset, kHdrMid, left, theme::kTextSecondary);

	char b[6];
	int pct = battery.percent();
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	snprintf(b, sizeof(b), "%d%%", pct);
	int16_t rightX = t.width() - theme::kCornerSafeInset;
	text(rightX, kHdrMid, b, theme::kTextPrimary, AlignR);

	if (extraRight) {
		t.setFont(nullptr);
		t.setTextSize(1);
		int16_t x1, y1;
		uint16_t w, h;
		t.getTextBounds(b, 0, 0, &x1, &y1, &w, &h);
		text(rightX - (int16_t)w - 8, kHdrMid, extraRight,
		     extraColor ? extraColor : theme::kTextSecondary, AlignR);
	}
}

// One centred footer line. `cy` is its centre; default sits it just inside
// the bottom edge.
void footerLine(const char *s, uint16_t color = theme::kTextHint, int16_t cy = 226) {
	text(C().width() / 2, cy, s, color, AlignC);
}

void segBar(int16_t x, int16_t y, int16_t w, int level, int maxLevel, uint16_t color,
            int16_t segH = 10) {
	if (maxLevel < 1) maxLevel = 8;
	if (maxLevel > 8) maxLevel = 8;
	const int16_t gap = 3;
	const int16_t segW = (w - (maxLevel - 1) * gap) / maxLevel;
	for (int i = 0; i < maxLevel; i++)
		C().fillRoundRect(x + i * (segW + gap), y, segW, segH, 2,
		                  i < level ? color : theme::kDivider);
}

// A device row inside a band [y, y+bandH): identity chip + name + pill, all
// vertically centred on the band.
void chip(int16_t x, int16_t y, Device *d, int16_t size, int16_t radius);

void deviceRow(int16_t y, Device *d, const char *name, const char *pillText, uint16_t pillFill,
               uint16_t pillFg, int16_t bandH = 26, const GFXfont *nameFont = &FreeSansBold9pt7b) {
	Adafruit_GFX &t = C();
	int16_t cs = bandH >= 26 ? 26 : 22;
	int16_t cy = y + bandH / 2;
	chip(theme::kPadH, cy - cs / 2, d, cs, 8);
	text(theme::kPadH + cs + 9, cy, name, theme::kTextPrimary, AlignL, nameFont);
	if (pillText) pill(t.width() - theme::kPadH, cy, pillText, pillFill, pillFg);
}

// Left label + right value in a 34px band; returns the band height.
int16_t prefRow(int16_t y, const char *label, const char *value, bool selected,
                uint16_t valueColor = theme::kTextPrimary) {
	Adafruit_GFX &t = C();
	const int16_t h = 34, w = t.width() - 2 * theme::kPadH;
	if (selected) t.fillRoundRect(theme::kPadH, y, w, h, theme::kListItemRadius, theme::kOkFill);
	int16_t cy = y + h / 2;
	text(theme::kPadH + 12, cy, label, selected ? theme::kOkText : theme::kTextInactive, AlignL,
	     &FreeSans9pt7b);
	if (value)
		text(t.width() - theme::kPadH - 12, cy, value, selected ? theme::kOkText : valueColor,
		     AlignR);
	return h;
}

// Full-width selectable list row, 34px band. Selected = green plate + bold.
// Returns band height so callers can stack rows.
int16_t listItem(int16_t y, const char *label, bool selected,
                 uint16_t normalColor = theme::kTextInactive) {
	Adafruit_GFX &t = C();
	const int16_t h = 34;
	if (selected)
		t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, h, theme::kListItemRadius,
		                theme::kOkFill);
	text(theme::kPadH + 12, y + h / 2, label, selected ? theme::kOkText : normalColor, AlignL,
	     selected ? &FreeSansBold9pt7b : &FreeSans9pt7b);
	return h;
}

const char *seenText(SeenState s) {
	switch (s) {
	case SeenState::Connected: return "LINKED NOW";
	case SeenState::EarlierThisBoot: return "SEEN THIS BOOT";
	default: return "NOT SEEN";
	}
}

// Identity chip: rounded square in the device hue with its pictogram
// (Device::drawGlyph) on top.
void chip(int16_t x, int16_t y, Device *d, int16_t size = 26, int16_t radius = 8) {
	Adafruit_GFX &t = C();
	t.fillRoundRect(x, y, size, size, radius, d->identityColor565());
	d->drawGlyph(t, x, y, size, d->identityTextColor565());
}

} // namespace

// ===========================================================================
// render dispatch
// ===========================================================================

void Menu::render() {
	if (ota.state() != Ota::State::Idle) {
		renderOtaActive();
		_shownScreen = _screen;
		display.flush();
		return;
	}

	if (mainLostTakeoverActive()) {
		if (_lostSinceMs == 0) _lostSinceMs = millis();
		renderMainLost();
	} else {
		switch (_screen) {
		case Screen::Rigs:         renderRigs(); break;
		case Screen::RigMenu:      renderRigMenu(); break;
		case Screen::Editor:       renderEditor(); break;
		case Screen::TextEntry:    renderTextEntry(); break;
		case Screen::Connecting:   renderConnecting(); break;
		case Screen::Control:      renderControl(); break;
		case Screen::Settings:     renderSettings(); break;
		case Screen::Devices:      renderDevices(); break;
		case Screen::DeviceCard:   renderDeviceCard(); break;
		case Screen::Scan:         renderScan(); break;
		case Screen::ControlPrefs: renderControlPrefs(); break;
		case Screen::ScreenPrefs:  renderScreenPrefs(); break;
		case Screen::System:       renderSystem(); break;
		}
	}
	_shownScreen = _screen;
	_shownTake = _takeActive;
	_shownMainLost = mainLostTakeoverActive();
	display.flush();
}

void Menu::renderDynamic() {
	if (_screen != _shownScreen || _takeActive != _shownTake ||
	    mainLostTakeoverActive() != _shownMainLost) {
		render();
		return;
	}
	// The whole frame is composited off-screen then flushed once, so a full
	// recompute stays tear-free — cheap enough for these screens.
	switch (_screen) {
	case Screen::Connecting:
	case Screen::Control:
	case Screen::Scan:
	case Screen::Devices:
	case Screen::DeviceCard:
		render();
		return;
	default:
		break;
	}
	if (mainLostTakeoverActive()) {
		render();
		return;
	}
	display.flush();
}

// ===========================================================================
// 1 / 16 / 17 — Configurations (home)
// ===========================================================================

void Menu::renderRigs() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);

	bool conn, connecting;
	anyLinks(conn, connecting);
	chromeBattery("CONFIGS", connecting ? theme::kWarnFill : theme::kOkFill);

	int n = rigStore.count();
	if (n == 0) {
		t.drawRoundRect(theme::kPadH, 74, t.width() - 2 * theme::kPadH, 56, 11, theme::kBorder);
		centerText("NO RIGS YET", 96, theme::kTextSecondary, &FreeSans9pt7b);
		footerLine("BUILD ONE FROM DEVICES", theme::kTextHint, 146);
		footerLine("OK:NEW RIG", theme::kTextHint, 206);
		footerLine("HOLD <:DEVICES");
		return;
	}

	// 3-row window around the cursor.
	int first = _rigCursor - 1;
	if (first < 0) first = 0;
	if (first > n - 3) first = n - 3;
	if (first < 0) first = 0;
	const int visible = n < 3 ? n : 3;

	int16_t y = 32;
	for (int slot = 0; slot < visible; slot++) {
		int i = first + slot;
		const Rig &r = rigStore.at(i);
		bool sel = i == _rigCursor;
		char comp[40];
		{
			comp[0] = '\0';
			Device *m = deviceAt(r.mainIndex);
			if (m) { strncat(comp, m->abbrev(), sizeof(comp) - strlen(comp) - 1); }
			for (int s = 0; s < r.secondaryCount; s++) {
				Device *d = deviceAt(r.secondary[s]);
				if (!d) continue;
				strncat(comp, comp[0] ? " + " : "", sizeof(comp) - strlen(comp) - 1);
				strncat(comp, d->abbrev(), sizeof(comp) - strlen(comp) - 1);
			}
			if (!comp[0]) strcpy(comp, "empty");
		}

		const int16_t rh = sel ? 46 : 40;
		if (sel)
			t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, rh, 11, theme::kOkFill);
		uint16_t nameCol = sel ? theme::kOkText : theme::kTextInactive;
		uint16_t compCol = sel ? theme::kOkText : theme::kTextHint;
		text(theme::kPadH + 12, y + 17, r.name, nameCol, AlignL,
		     sel ? &FreeSansBold9pt7b : &FreeSans9pt7b);
		text(theme::kPadH + 12, y + rh - 10, comp, compCol, AlignL);
		if (sel && i == settings.rigIndex())
			text(t.width() - theme::kPadH - 12, y + 12, "LAST", theme::kOkText, AlignR);
		y += rh + 6;
	}

	// Scrollbar.
	if (n > 3) {
		int16_t trackY = 32, trackH = 134;
		t.fillRoundRect(t.width() - 6, trackY, 3, trackH, 2, theme::kDivider);
		int16_t thumbH = trackH * 3 / n;
		int16_t thumbY = trackY + (trackH - thumbH) * first / (n - 3);
		t.fillRoundRect(t.width() - 6, thumbY, 3, thumbH, 2, theme::kTextHint);
	}

	char pos[12];
	snprintf(pos, sizeof(pos), "%d/%d", _rigCursor + 1, n);
	footerLine(pos, theme::kTextSecondary, 188);
	footerLine("OK:CONNECT  >:MENU", theme::kTextHint, 206);
	footerLine("HOLD <:DEVICES");
}

// ===========================================================================
// 18 — Rig menu
// ===========================================================================

void Menu::renderRigMenu() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	chrome(rigStore.at(_rigCursor).name, "RIG", theme::kOkFill, false);

	static const char *items[] = {"Edit", "Duplicate", "Rename", "Delete"};
	int16_t y = 40;
	for (int i = 0; i < 4; i++) {
		bool sel = i == _rigMenuCursor;
		y += listItem(y, items[i], sel, i == 3 ? theme::kRecordFill : theme::kTextInactive) + 7;
	}
	footerLine("OK:SELECT  <:BACK");
}

// ===========================================================================
// 11-13 — Rig editor (step 0 name handled by TextEntry)
// ===========================================================================

void Menu::renderEditor() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);

	char step[16];
	snprintf(step, sizeof(step), "EDIT STEP %d/4", _editStep + 1);
	chrome(step, _editRig.name, theme::kOkFill, false);

	if (_editStep == 1) {
		text(theme::kPadH, 34, "Main device", theme::kTextPrimary, AlignL, &FreeSansBold9pt7b);

		int rows = 0, motion[8] = {0};
		for (int i = 0; i < deviceCount(); i++)
			if (deviceAt(i)->kind() == DeviceKind::Motion) motion[rows++] = i;

		int16_t y = 56;
		for (int k = 0; k <= rows; k++) {
			const char *label = k < rows ? deviceAt(motion[k])->name() : "None";
			y += listItem(y, label, k == _editCursor) + 7;
		}
		footerLine("ONE MAIN ONLY", theme::kTextHint, 200);
		footerLine("OK:NEXT  HOLD <:BACK");
	} else if (_editStep == 2) {
		text(theme::kPadH, 34, "Secondary", theme::kTextPrimary, AlignL, &FreeSansBold9pt7b);

		int rows = 0, cams[8] = {0};
		for (int i = 0; i < deviceCount(); i++)
			if (deviceAt(i)->kind() == DeviceKind::Camera) cams[rows++] = i;

		int16_t y = 56;
		for (int k = 0; k < rows; k++) {
			int dev = cams[k];
			bool sel = k == _editCursor;
			if (sel)
				t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, 32, 11, theme::kDivider);
			int16_t bx = theme::kPadH + 12, by = y + 8;
			bool checked = _editRig.hasSecondary(dev);
			if (checked) {
				t.fillRoundRect(bx, by, 16, 16, 4, theme::kOkFill);
				text(bx + 8, y + 16, "x", theme::kOkText, AlignC);
			} else {
				t.drawRoundRect(bx, by, 16, 16, 4, theme::kTextHint);
			}
			text(bx + 26, y + 16, deviceRegistry.alias(dev),
			     sel ? theme::kTextPrimary : theme::kTextInactive, AlignL, &FreeSans9pt7b);
			y += 32 + 6;
		}
		footerLine("MULTIPLE PHONES OK", theme::kTextHint, 200);
		footerLine(">:TOGGLE  OK:NEXT");
	} else { // step 3
		text(theme::kPadH, 34, "Take mode", theme::kTextPrimary, AlignL, &FreeSansBold9pt7b);

		struct { const char *title; const char *sub; } opt[2] = {
			{"Record", "OK:CAMS ONLY  MOVE ON ARROWS"},
			{"Record + move", "OK RECS AND MOVES MAIN"},
		};
		int16_t y = 58;
		for (int i = 0; i < 2; i++) {
			bool sel = i == _editCursor;
			if (sel)
				t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, 46, 11, theme::kOkFill);
			text(theme::kPadH + 12, y + 17, opt[i].title, sel ? theme::kOkText : theme::kTextInactive,
			     AlignL, &FreeSansBold9pt7b);
			text(theme::kPadH + 12, y + 34, opt[i].sub, sel ? theme::kOkText : theme::kTextHint);
			y += 46 + 8;
		}
		footerLine("MODE LABELED ON CONTROL", theme::kTextHint, 200);
		footerLine("OK:SAVE  HOLD <:BACK");
	}
}

// ===========================================================================
// 19 — Text entry
// ===========================================================================

void Menu::renderTextEntry() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);

	const char *title = _textReturn == TextReturn::DeviceRename ? "DEVICE NAME" : "RIG NAME";
	chrome(_textReturn == TextReturn::EditorName ? "EDIT STEP 1/4" : "RENAME", title,
	       theme::kOkFill, false);

	text(theme::kPadH, 32, _textReturn == TextReturn::DeviceRename ? "New device name" : "Rig name",
	     theme::kTextPrimary, AlignL, &FreeSansBold9pt7b);

	// Field with block cursor.
	int16_t fx = theme::kPadH, fy = 70, fw = t.width() - 2 * theme::kPadH, fh = 44;
	t.drawRoundRect(fx, fy, fw, fh, 11, theme::kBorder);
	t.setFont(&FreeMonoBold18pt7b);
	t.setTextSize(1);
	int16_t cx = fx + 12;
	int16_t baseY = fy + 30;
	for (int i = 0; i <= _textPos && _textBuf[i]; i++) {
		char ch[2] = {_textBuf[i], 0};
		int16_t x1, y1;
		uint16_t w, h;
		t.getTextBounds(ch, 0, 0, &x1, &y1, &w, &h);
		if (i == _textPos) {
			t.fillRect(cx - 1, fy + 8, (int16_t)w + 2, 26, theme::kOkFill);
			t.setTextColor(theme::kOkText);
		} else {
			t.setTextColor(theme::kTextPrimary);
		}
		t.setCursor(cx, baseY);
		t.print(ch);
		cx += (int16_t)w + 1;
	}
	t.setFont(nullptr);

	// Letter ribbon around the current character.
	char curCh = _textBuf[_textPos] ? _textBuf[_textPos] : ' ';
	for (int d = -4; d <= 4; d++) {
		char c = (curCh >= 'A' && curCh <= 'Z') ? 'A' + (curCh - 'A' + d + 26) % 26
		         : (curCh >= '0' && curCh <= '9') ? '0' + (curCh - '0' + d + 10) % 10
		                                          : curCh;
		char s[2] = {c, 0};
		text(t.width() / 2 + d * 28, 148, s, d == 0 ? theme::kTextPrimary : theme::kTextHint, AlignC,
		     &FreeMonoBold18pt7b);
	}

	footerLine("UP/DN:CHAR  L/R:POS  HOLD >:DEL", theme::kTextHint, 202);
	footerLine("OK:NEXT  HOLD <:CANCEL");
}

// ===========================================================================
// 2 / 3 — Connecting
// ===========================================================================

void Menu::renderConnecting() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	const Rig *r = rigAt(_activeRig);

	bool timedOut = (millis() - _connectStartedMs) > kConnectTimeoutMs;
	chromeBattery(r ? r->name : "CONNECTING", theme::kWarnFill);
	if (timedOut) t.drawRect(0, 0, t.width(), t.height(), theme::kWarnFill);

	int16_t y = 30;
	Device *m = rigMainOf(_activeRig);

	if (timedOut) {
		centerText("NO RESPONSE", 54, theme::kTextPrimary, nullptr, 2);
		y = 86;
	}

	if (m && !timedOut) {
		text(theme::kPadH, y + 4, "MAIN", theme::kTextHint);
		y += 16;
		t.drawRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, 40, 11, theme::kBorder);
		bool up = m->isConnected();
		deviceRow(y, m, deviceRegistry.alias(deviceIndexOf(m)), up ? "READY" : "LINKING",
		          up ? theme::kOkFill : theme::kWarnFill, up ? theme::kOkText : theme::kWarnText, 40);
		y += 40 + 14;
	}

	if (r && r->secondaryCount > 0) {
		text(theme::kPadH, y + 4, m ? "SECONDARY" : "CAMERAS", theme::kTextHint);
		y += 16;
		for (int i = 0; i < r->secondaryCount; i++) {
			Device *d = deviceAt(r->secondary[i]);
			if (!d) continue;
			bool up = d->isConnected();
			deviceRow(y, d, deviceRegistry.alias(r->secondary[i]),
			          up ? "READY" : (timedOut ? "NO LINK" : "SCAN"),
			          up ? theme::kOkFill : (timedOut ? theme::kWarnFill : theme::kDivider),
			          up ? theme::kOkText : (timedOut ? theme::kWarnText : theme::kTextInactive), 28,
			          &FreeSans9pt7b);
			y += 32;
		}
	}

	if (timedOut) {
		footerLine("CHECK DEVICE POWER", theme::kTextSecondary, 170);
		footerLine("OK:RETRY", theme::kTextHint, 206);
		footerLine("HOLD <:CANCEL");
	} else {
		int16_t barW = t.width() - 2 * theme::kPadH;
		t.fillRoundRect(theme::kPadH, 172, barW, 3, 2, theme::kDivider);
		t.fillRoundRect(theme::kPadH, 172, barW * 2 / 5, 3, 2, theme::kWarnFill);
		footerLine("OPENS WHEN MAIN READY", theme::kTextSecondary, 186);
		footerLine("HOLD <:CANCEL");
	}
}

// ===========================================================================
// 4-9 — Control
// ===========================================================================

void Menu::renderControl() {
	const Rig *r = rigAt(_activeRig);
	if (!r) { C().fillScreen(theme::kBackground); return; }
	if (rigMainOf(_activeRig)) renderControlMotion(*r);
	else renderControlCamerasOnly(*r);
}

void Menu::renderControlMotion(const Rig &r) {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);

	Device *m = rigMainOf(_activeRig);
	int ready, total;
	rigPhoneCounts(_activeRig, ready, total);

	// Fixed grid straight off mock screens 4 / 6 — no floating flow.
	const int16_t PAD = theme::kPadH;             // 13
	const int16_t W = t.width() - 2 * PAD;        // 254
	const int lvl = m->speedLevel(), mx = m->speedLevelMax();
	const bool speedUD = settings.axisBinding() == AxisBinding::SpeedUpDown;

	uint16_t bar = _takeActive ? theme::kRecordFill
	               : (total > 0 && ready == 0) ? theme::kWarnFill
	                                           : theme::kOkFill;
	char cam[12];
	if (total > 0) snprintf(cam, sizeof(cam), "CAM %d/%d", ready, total);
	chromeBattery(r.name, bar, total > 0 ? cam : nullptr,
	              ready == total ? theme::kOkFill : theme::kWarnFill);
	if (_takeActive) t.drawRect(0, 0, t.width(), t.height(), theme::kRecordFill);

	// State pill: real device telemetry where available (slider), else the
	// Menu's own locally-tracked jog state (dolly).
	const bool prog = m->programName() != nullptr;
	const bool fault = m->inFault();
	const char *st = m->motionStateText();
	if (!st)
		st = _mainMotion == MainMotion::Forward    ? "RUN >"
		     : _mainMotion == MainMotion::Backward ? "RUN <"
		                                           : "IDLE";
	bool moving;
	if (fault) moving = false;
	else if (m->motionStateText())
		moving = strcmp(st, "IDLE") != 0 && strcmp(st, "SLEEP") != 0;
	else moving = _mainMotion != MainMotion::Stopped;
	uint16_t stF = fault ? theme::kRecordFill : moving ? theme::kOkFill : theme::kDivider;
	uint16_t stT = fault ? theme::kRecordText : moving ? theme::kOkText : theme::kTextPrimary;

	int16_t x1, y1;
	uint16_t w, h;

	if (_takeActive) {
		// Red timer block — mock 6: y26 h64.
		const int16_t by = 26, bh = 64;
		t.fillRoundRect(PAD, by, W, bh, 11, theme::kRecordFill);
		uint32_t el = _recordStartedAtMs ? (millis() - _recordStartedAtMs) / 1000 : 0;
		char tm[12];
		snprintf(tm, sizeof(tm), "%02lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
		text(PAD + 14, by + bh / 2, tm, theme::kRecordText, AlignL, &FreeMonoBold24pt7b);
		char cc[14];
		snprintf(cc, sizeof(cc), "%d/%d CAM", ready, total);
		text(PAD + W - 12, by + 22, "* REC", theme::kRecordText, AlignR);
		text(PAD + W - 12, by + 42, cc, theme::kRecordText, AlignR);

		// Main row — mock 6: y100.  Phone-lost strip pushes it to y128.
		int16_t rowY = 100;
		if (total > 1 && ready < total) {
			t.fillRoundRect(PAD, 100, W, 22, 9, theme::kWarnFill);
			text(PAD + 10, 111, "! A CAMERA LOST LINK", theme::kWarnText);
			rowY = 128;
		}
		char pillTxt[16];
		snprintf(pillTxt, sizeof(pillTxt), "%s %d/%d", st, lvl, mx);
		deviceRow(rowY, m, m->name(), pillTxt, stF, stT, 26, &FreeSans9pt7b);

		segBar(PAD, rowY + 34, W, lvl, mx, m->identityColor565(), 8);
		text(PAD, rowY + 52, speedUD ? "UP/DN:SPEED  L/R:MOVE" : "L/R:SPEED  UP/DN:MOVE",
		     theme::kTextHint);

		t.fillRoundRect(PAD, 166, W, 34, 11, theme::kRecordFill);
		centerText("OK - STOP REC", 183, theme::kRecordText, &FreeSansBold9pt7b);
		footerLine("HOLD OK - STOP ALL", theme::kRecordFill);
		return;
	}

	// --- Not recording: mock screen 4 / 5 fixed grid ---
	deviceRow(28, m, m->name(), st, stF, stT, 26, &FreeSansBold9pt7b);

	// SPEED, left column.
	text(PAD, 66, "SPEED", theme::kTextHint);
	char num[4];
	snprintf(num, sizeof(num), "%d", lvl);
	text(PAD, 92, num, theme::kTextPrimary, AlignL, &FreeMonoBold24pt7b);
	t.setFont(&FreeMonoBold24pt7b);
	t.setTextSize(1);
	t.getTextBounds(num, 0, 0, &x1, &y1, &w, &h);
	t.setFont(nullptr);
	char den[6];
	snprintf(den, sizeof(den), "/%d", mx);
	text(PAD + (int16_t)w + 6, 100, den, theme::kTextSecondary);

	// Right column.
	if (prog) {
		// Program-driven Main (slider): name + endstop/calibration telemetry
		// (no manual jog on this screen — see docs/11_program_api.md).
		text(t.width() - PAD, 66, m->programName(), theme::kTextInactive, AlignR, &FreeSansBold9pt7b);
		int16_t dx = t.width() - PAD - 6;
		t.fillCircle(dx, 92, 4, m->endstop2() ? theme::kWarnFill : theme::kDivider);
		t.fillCircle(dx - 16, 92, 4, m->endstop1() ? theme::kWarnFill : theme::kDivider);
		text(dx - 30, 92, "E1  E2", theme::kTextHint, AlignR);
	} else {
		text(t.width() - PAD, 66, "TRAVEL", theme::kTextHint, AlignR);
		int16_t homeW = m->supportsHome() ? 46 : 0;
		int16_t ax = t.width() - PAD - homeW - 30;
		int16_t ay = 92;
		uint16_t lc = _mainMotion == MainMotion::Backward ? theme::kOkFill : theme::kBorder;
		uint16_t rc = _mainMotion == MainMotion::Forward ? theme::kOkFill : theme::kBorder;
		t.fillTriangle(ax, ay, ax + 10, ay - 7, ax + 10, ay + 7, lc);
		t.fillTriangle(ax + 18, ay - 7, ax + 18, ay + 7, ax + 28, ay, rc);
		if (m->supportsHome()) pill(t.width() - PAD, ay, "HOME", theme::kDivider, theme::kTextInactive);
	}

	segBar(PAD, 120, W, lvl, mx, m->identityColor565());
	text(PAD, 140,
	     prog ? "UP/DN L/R:SPEED   HOLD>:HOME"
	          : (speedUD ? (m->supportsHome() ? "UP/DN:SPEED  L/R:MOVE  HOLD>:HOME"
	                                          : "UP/DN:SPEED  L/R:MOVE")
	                     : "L/R:SPEED  UP/DN:MOVE"),
	     theme::kTextHint);

	// Bottom action block — mock 4: y160 h38.
	const int16_t ry = 160, rh = 38, rcy = ry + rh / 2;
	if (fault) {
		t.fillRoundRect(PAD, ry, W, rh, 11, theme::kRecordFill);
		centerText("FAULT - HOLD OK TO CLEAR", rcy, theme::kRecordText, &FreeSansBold9pt7b);
		footerLine("HOLD OK - E-STOP   HOLD < - EXIT");
	} else if (ready > 0) {
		t.drawRoundRect(PAD, ry, W, rh, 11, theme::kRecordFill);
		t.fillCircle(PAD + 18, rcy, 5, theme::kRecordFill);
		text(PAD + 32, rcy, "OK - REC", theme::kTextPrimary, AlignL, &FreeSansBold9pt7b);
		char cc[10];
		snprintf(cc, sizeof(cc), "%d CAM", ready);
		text(t.width() - PAD - 14, rcy, cc, theme::kOkFill, AlignR);
		footerLine("HOLD OK - E-STOP");
	} else if (total > 0) {
		t.fillRoundRect(PAD, ry, W, rh, 11, theme::kDivider);
		text(PAD + 12, ry + 13, "REC UNAVAILABLE", theme::kWarnFill, AlignL, &FreeSansBold9pt7b);
		text(PAD + 12, ry + 27, "A CAMERA HAS NO LINK", theme::kTextInactive);
		footerLine("HOLD OK - E-STOP   HOLD < - EXIT");
	} else if (prog) {
		// No camera: Ok is the program's START / STOP.
		bool run = m->programRunning();
		t.fillRoundRect(PAD, ry, W, rh, 11, run ? theme::kOkFill : theme::kBorder);
		char lbl[24];
		snprintf(lbl, sizeof(lbl), "OK - %s", run ? "STOP" : "START");
		centerText(lbl, rcy, run ? theme::kOkText : theme::kTextPrimary, &FreeSansBold9pt7b);
		footerLine("HOLD OK - E-STOP   HOLD < - EXIT");
	} else {
		t.drawRoundRect(PAD, ry, W, rh, 11, theme::kBorder);
		centerText("MOTION ONLY - NO CAMERA", rcy, theme::kTextInactive, &FreeSans9pt7b);
		footerLine("HOLD OK - E-STOP   HOLD < - EXIT");
	}
}

void Menu::renderControlCamerasOnly(const Rig &r) {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	int ready, total;
	rigPhoneCounts(_activeRig, ready, total);
	const int16_t PAD = theme::kPadH, W = t.width() - 2 * PAD;
	const bool multi = phoneDevice.bondedPhoneCount() > 1;

	chromeBattery(r.name, _takeActive ? theme::kRecordFill : theme::kPhoneFill);
	if (_takeActive) t.drawRect(0, 0, t.width(), t.height(), theme::kRecordFill);

	// Up to 2 phone rows, mock 9: first at y36, 44px pitch.
	int16_t y = 36;
	int shown = 0;
	for (int i = 0; i < r.secondaryCount && shown < 2; i++) {
		Device *d = deviceAt(r.secondary[i]);
		if (!d || d->kind() != DeviceKind::Camera) continue;
		bool up = d->isConnected();
		const char *nm = (multi && up) ? phoneDevice.activePhoneLabel()
		                               : deviceRegistry.alias(r.secondary[i]);
		t.drawRoundRect(PAD, y, W, 36, 11, theme::kBorder);
		deviceRow(y, d, nm, up ? "READY" : "LINK",
		          up ? theme::kOkFill : theme::kWarnFill, up ? theme::kOkText : theme::kWarnText, 36,
		          &FreeSans9pt7b);
		y += 44;
		shown++;
	}

	y += 12;
	if (_takeActive) {
		uint32_t el = _recordStartedAtMs ? (millis() - _recordStartedAtMs) / 1000 : 0;
		char tm[12];
		snprintf(tm, sizeof(tm), "%02lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
		const int16_t bh = 58;
		t.fillRoundRect(PAD, y, W, bh, 11, theme::kRecordFill);
		centerText(tm, y + bh / 2, theme::kRecordText, &FreeMonoBold24pt7b);
		char cc[18];
		snprintf(cc, sizeof(cc), "* REC   %d/%d CAM", ready, total);
		centerText(cc, y + bh + 14, theme::kRecordFill);
		footerLine("OK - STOP REC   HOLD OK - STOP ALL", theme::kRecordFill);
	} else {
		const int16_t bh = 46, bcy = y + bh / 2;
		t.drawRoundRect(PAD, y, W, bh, 11, theme::kRecordFill);
		t.setFont(&FreeSansBold12pt7b);
		t.setTextSize(1);
		int16_t x1, y1;
		uint16_t w, h;
		t.getTextBounds("OK - REC", 0, 0, &x1, &y1, &w, &h);
		t.setFont(nullptr);
		int16_t tx = (t.width() + 22 - (int16_t)w) / 2;
		t.fillCircle(tx - 16, bcy, 5, theme::kRecordFill);
		text(tx, bcy, "OK - REC", theme::kTextPrimary, AlignL, &FreeSansBold12pt7b);

		footerLine(total > 1 ? "START GOES TO ALL CAMS" : "START GOES TO THE CAMERA",
		           theme::kTextSecondary, 192);
		footerLine(multi ? ">:SWITCH PHONE   HOLD <:CONFIGS" : "HOLD <:CONFIGS");
	}
}

// ===========================================================================
// 10 — Main lost
// ===========================================================================

void Menu::renderMainLost() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	const Rig *r = rigAt(_activeRig);
	chromeBattery(r ? r->name : "SESSION", theme::kWarnFill);
	t.drawRect(0, 0, t.width(), t.height(), theme::kWarnFill);

	Device *m = rigMainOf(_activeRig);
	if (!m) return;

	chip((t.width() - 34) / 2, 40, m, 34, 9);

	char lost[28];
	snprintf(lost, sizeof(lost), "%s lost", m->name());
	centerText(lost, 98, theme::kTextPrimary, &FreeSansBold12pt7b);

	pill(t.width() / 2 + 54, 122, "MOTION STOPPED", theme::kWarnFill, theme::kWarnText);
	footerLine("REC STOPPED ON ALL CAMS", theme::kTextSecondary, 150);
	footerLine("PHONES STILL LINKED", theme::kTextHint, 166);

	bool failed = (millis() - _lostSinceMs) > 30000;
	footerLine(failed ? "FAILED - OK:RETRY" : "RECONNECTING...", theme::kTextHint, 202);
	footerLine("HOLD <:CONFIGS");
}

// ===========================================================================
// 14 — Settings
// ===========================================================================

void Menu::renderSettings() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	char pos[8];
	snprintf(pos, sizeof(pos), "%d/5", _settingsCursor + 1);
	chrome("SETTINGS", pos, theme::kOkFill, false);

	static const char *items[] = {"Configs", "Devices", "Control", "Screen", "System"};
	int16_t y = 38;
	for (int i = 0; i < 5; i++) {
		bool sel = i == _settingsCursor;
		listItem(y, items[i], sel);
		if (i == 3) { // brightness mini-bar on the Screen row
			int16_t bw = 56, bx = t.width() - theme::kPadH - 12 - bw, by = y + 15;
			t.fillRoundRect(bx, by, bw, 4, 2, theme::kDivider);
			t.fillRoundRect(bx, by, bw * settings.brightness() / 255, 4, 2,
			                sel ? theme::kOkText : theme::kTextInactive);
		}
		y += 34 + 6;
	}
	footerLine("UP/DN  OK:OPEN  <:BACK");
}

// ===========================================================================
// 15 — Devices registry
// ===========================================================================

void Menu::renderDevices() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	char cnt[6];
	snprintf(cnt, sizeof(cnt), "%d", deviceCount());
	chrome("DEVICES", cnt, theme::kPhoneFill, false);

	int16_t y = 34;
	for (int i = 0; i < deviceCount(); i++) {
		Device *d = deviceAt(i);
		bool sel = i == _devicesCursor;
		if (sel)
			t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, 40, 11, theme::kDivider);
		chip(theme::kPadH + 8, y + 9, d, 22, 7);
		text(theme::kPadH + 40, y + 15, deviceRegistry.alias(i),
		     sel ? theme::kTextPrimary : theme::kTextInactive, AlignL, &FreeSans9pt7b);
		char meta[36];
		snprintf(meta, sizeof(meta), "%s . %s", d->kind() == DeviceKind::Motion ? "MOTION" : "CAMERA",
		         seenText(deviceRegistry.seen(i)));
		text(theme::kPadH + 40, y + 29, meta, theme::kTextHint);
		y += 40 + 8;
	}
	footerLine(">:RENAME, DELETE, TEST", theme::kTextHint, 192);
	footerLine("OK:ADD  <:BACK");
}

// ===========================================================================
// 21 — Device card
// ===========================================================================

void Menu::renderDeviceCard() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	Device *d = deviceAt(_cardDevice);
	if (!d) { _screen = Screen::Devices; return; }

	chrome("DEVICE", d->isConnected() ? "LINKED" : "OFFLINE", theme::kOkFill, false);

	chip(theme::kPadH, 26, d, 32, 9);
	text(theme::kPadH + 42, 36, deviceRegistry.alias(_cardDevice), theme::kTextPrimary, AlignL,
	     &FreeSansBold9pt7b);
	char sub[28];
	snprintf(sub, sizeof(sub), "%s . BLE", d->kind() == DeviceKind::Motion ? "MOTION" : "CAMERA");
	text(theme::kPadH + 42, 52, sub, theme::kTextHint);

	t.drawFastHLine(theme::kPadH, 72, t.width() - 2 * theme::kPadH, theme::kDivider);
	auto kv = [&](int16_t yy, const char *k, const char *v) {
		text(theme::kPadH, yy, k, theme::kTextHint);
		text(t.width() - theme::kPadH, yy, v, theme::kTextInactive, AlignR);
	};
	kv(86, "ID", d->advertisedName()[0] ? d->advertisedName() : "-");
	kv(103, "LAST SEEN", seenText(deviceRegistry.seen(_cardDevice)));
	char rc[6];
	snprintf(rc, sizeof(rc), "%d", deviceRegistry.rigMembership(_cardDevice));
	kv(120, "IN RIGS", rc);

	const bool canSwitch = d->kind() == DeviceKind::Camera && phoneDevice.bondedPhoneCount() > 1;
	const char *acts[] = {"Test link", "Rename", "Switch phone"};
	const int actCount = canSwitch ? 3 : 2;
	int16_t y = 138;
	for (int i = 0; i < actCount; i++)
		y += listItem(y, acts[i], i == _cardCursor) + 4;
	footerLine("UP/DN  OK:DO  <:BACK");
}

// ===========================================================================
// 20 — Scan
// ===========================================================================

void Menu::renderScan() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	chrome("SCAN", _scanning ? "SCANNING" : (_scanCount ? "DONE" : "IDLE"), theme::kWarnFill);

	if (_scanCount == 0) {
		centerText(_scanning ? "SCANNING..." : "OK TO SCAN", 90, theme::kTextSecondary,
		           &FreeSans9pt7b);
	} else {
		int16_t y = 34;
		int first = _scanCursor - 2;
		if (first < 0) first = 0;
		if (first > _scanCount - 4) first = _scanCount - 4;
		if (first < 0) first = 0;
		for (int k = 0; k < 4 && first + k < _scanCount; k++) {
			int i = first + k;
			bool sel = i == _scanCursor;
			if (sel)
				t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, 36, 11, theme::kDivider);
			text(theme::kPadH + 10, y + 13, _scan[i].name,
			     sel ? theme::kTextPrimary : theme::kTextInactive, AlignL, &FreeSans9pt7b);
			char rssi[12];
			snprintf(rssi, sizeof(rssi), "%d dBm", _scan[i].rssi);
			text(theme::kPadH + 10, y + 27, rssi, theme::kTextHint);
			y += 40;
		}
	}

	footerLine("PHONE ADDS ITSELF FROM PHONE", theme::kTextHint, 190);
	footerLine(_scanCount ? "OK:RESCAN  <:BACK" : "OK:SCAN  <:BACK");
}

// ===========================================================================
// 22 — Control prefs
// ===========================================================================

void Menu::renderControlPrefs() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	chrome("CONTROL", nullptr, theme::kOkFill, false);

	// Axes card.
	bool axSel = _ctrlPrefCursor == 0;
	int16_t y = 34;
	t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, 42, 11,
	                axSel ? theme::kOkFill : theme::kDivider);
	text(theme::kPadH + 12, y + 15, "Axes", axSel ? theme::kOkText : theme::kTextInactive, AlignL,
	     &FreeSansBold9pt7b);
	text(theme::kPadH + 12, y + 30,
	     settings.axisBinding() == AxisBinding::SpeedUpDown ? "SPEED ON UP/DN  MOVE ON L/R"
	                                                        : "SPEED ON L/R  MOVE ON UP/DN",
	     axSel ? theme::kOkText : theme::kTextHint);
	y += 50;

	char spd[8];
	snprintf(spd, sizeof(spd), "%d/8", settings.maxSpeedLevel());
	y += prefRow(y, "Max speed", spd, _ctrlPrefCursor == 1);
	y += prefRow(y, "Autostart last", settings.autostartLastRig() ? "ON" : "OFF", _ctrlPrefCursor == 2,
	             settings.autostartLastRig() ? theme::kOkFill : theme::kTextHint);
	y += prefRow(y, "Key sound", settings.buttonSound() ? "ON" : "OFF", _ctrlPrefCursor == 3,
	             settings.buttonSound() ? theme::kOkFill : theme::kTextHint);

	footerLine("UP/DN:ITEM  L/R:VALUE  <:BACK");
}

// ===========================================================================
// Screen prefs (brightness)
// ===========================================================================

void Menu::renderScreenPrefs() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	chrome("SCREEN", nullptr, theme::kOkFill, false);

	char v[8];
	snprintf(v, sizeof(v), "%d", (_pendingBrightness * 100) / 255);
	centerText(v, 96, theme::kTextPrimary, &FreeMonoBold24pt7b);

	int16_t bw = t.width() - 2 * theme::kPadH, by = 128;
	t.drawRect(theme::kPadH, by, bw, 8, theme::kBorder);
	t.fillRect(theme::kPadH + 1, by + 1, (bw - 2) * _pendingBrightness / 255, 6, theme::kOkFill);

	footerLine("L/R:ADJUST  OK:SAVE  HOLD <:CANCEL");
}

// ===========================================================================
// 23 — System
// ===========================================================================

void Menu::renderSystem() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	chrome("SYSTEM", nullptr, theme::kOkFill, false);

	if (_sysConfirm != SysConfirm::None) {
		centerText(_sysConfirm == SysConfirm::Ota ? "START OTA?" : "FACTORY RESET?", 70,
		           theme::kTextPrimary, &FreeSansBold12pt7b);
		static const char *yn[] = {"YES", "NO"};
		int16_t y = 104;
		for (int i = 0; i < 2; i++) {
			y += listItem(y, yn[i], i == _sysConfirmSel) + 6;
		}
		footerLine("UP/DN  OK:CONFIRM  HOLD <:CANCEL");
		return;
	}

	int16_t y = 34;
	y += prefRow(y, "Firmware", "dev", _sysCursor == 0) + 4;
	char batt[16];
	snprintf(batt, sizeof(batt), "%d%% . %.2f V", battery.percent(), battery.voltage());
	y += prefRow(y, "Battery", batt, _sysCursor == 1) + 4;
	y += prefRow(y, "OTA update", nullptr, _sysCursor == 2) + 4;

	bool sel = _sysCursor == 3;
	if (sel)
		t.fillRoundRect(theme::kPadH, y, t.width() - 2 * theme::kPadH, 34, theme::kListItemRadius,
		                theme::kRecordFill);
	text(theme::kPadH + 12, y + 17, "Factory reset", sel ? theme::kRecordText : theme::kRecordFill,
	     AlignL, &FreeSans9pt7b);

	footerLine("OTA DISABLES BLE WHILE UPDATING", theme::kTextHint, 198);
	footerLine("OK:OPEN  <:BACK");
}

// ===========================================================================
// OTA active (mock: exclusive mode)
// ===========================================================================

void Menu::renderOtaActive() {
	Adafruit_GFX &t = C();
	t.fillScreen(theme::kBackground);
	t.drawRect(0, 0, t.width(), t.height(), theme::kWarnFill);

	text(theme::kCornerSafeInset, kHdrMid, "BLE: OFF", theme::kTextHint);
	text(t.width() - theme::kCornerSafeInset, kHdrMid, "WIFI: ON", theme::kWarnFill, AlignR);

	centerText("OTA MODE", 74, theme::kTextPrimary, nullptr, 2);

	switch (ota.state()) {
	case Ota::State::Connecting:
		centerText("Connecting WiFi...", 120, theme::kTextSecondary, &FreeSans9pt7b);
		break;
	case Ota::State::WaitingForUpload:
	case Ota::State::Uploading:
		centerText(ota.ip(), 128, theme::kWarnFill, &FreeMonoBold18pt7b);
		centerText(ota.state() == Ota::State::Uploading ? "Uploading..." : "Waiting for upload...",
		           156, theme::kTextSecondary, &FreeSans9pt7b);
		break;
	case Ota::State::Failed:
		centerText("Failed / timeout", 124, theme::kWarnFill, &FreeSans9pt7b);
		break;
	default:
		break;
	}

	t.fillRoundRect(theme::kPadH, t.height() - 40, t.width() - 2 * theme::kPadH, 18, 6,
	                theme::kWarnFill);
	t.setFont(nullptr);
	t.setTextColor(theme::kWarnText);
	t.setCursor(theme::kPadH + 6, t.height() - 35);
	t.print("OTHER FUNCTIONS DISABLED");
	footerLine("OK - EXIT");
}
