#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// Color/type/geometry constants transcribed from docs/screen-design.md
// (the code-facing distillation of the design handoff in docs/design/).
// RGB565, converted from the spec's "approx hex" columns.
namespace theme {

// --- Neutrals ---
constexpr uint16_t kBackground   = 0x0862; // #0d0e10
constexpr uint16_t kBorder       = 0x39E8; // #3a3c40
constexpr uint16_t kDivider      = 0x2966; // #2b2d30
constexpr uint16_t kTextPrimary  = 0xEF7D; // #ebecee
constexpr uint16_t kTextSecondary = 0x8C72; // #8b8d91
constexpr uint16_t kTextHint     = 0x5B0C; // #5f6165
constexpr uint16_t kTextInactive = 0xA535; // #a4a6aa

// --- Device identity (hue = which device) ---
constexpr uint16_t kPhoneFill  = 0x0BFA; // #0a7fd4
constexpr uint16_t kPhoneText  = 0xF7DF; // #f4fbff
constexpr uint16_t kSliderFill = 0x04A9; // #00944a
constexpr uint16_t kSliderText = 0x00C1; // #04180d
constexpr uint16_t kDollyFill  = 0xA27A; // #a04ed6
constexpr uint16_t kDollyText  = 0xFFBF; // #fdf5ff

// --- State (hue = what's happening) ---
constexpr uint16_t kOkFill      = 0x05AB; // #00b45b
constexpr uint16_t kOkText      = kSliderText; // same hue (140) family, reused per spec
constexpr uint16_t kRecordFill  = 0xC9C3; // #c8391f
constexpr uint16_t kRecordText  = 0xFFBE; // near-white, warm tint
constexpr uint16_t kWarnFill    = 0xDCC0; // #d99a00
constexpr uint16_t kWarnText    = 0x20C1; // near-black, warm tint

// --- Geometry, target 280x240 landscape panel px ---
// These values are 1:1 with the current Remote Screen handoff.
constexpr int16_t kPadH = 13;
constexpr int16_t kPadV = 10;
constexpr int16_t kCornerSafeInset = 28;
constexpr int16_t kIconChip = 26;
constexpr int16_t kIconChipRadius = 8;
constexpr int16_t kPillRadiusY = 10; // pill height ~20px -> fully rounded
constexpr int16_t kRowGap = 7;
constexpr int16_t kAccentBarWidth = 3;
constexpr int16_t kTopBarHeight = 3; // flat-bar fallback for the spec's vignette
constexpr int16_t kListItemRadius = 11;

// --- Typography: target px tiers -> Adafruit_GFX setTextSize() ---
// Built-in GFX font is 6px advance x 8px tall per setTextSize(1) unit and
// already fixed-width, which is why no custom font was added (see
// docs/screen-design.md "Typography" section).
constexpr uint8_t kSizeHint   = 1; // header/footer/badges, spec's 9-12px tier
constexpr uint8_t kSizeBody   = 2; // device name / list items, spec's 17-18px tier
constexpr uint8_t kSizeBigVal = 4; // OTA IP, spec's 30px tier
constexpr uint8_t kSizeTimer = 7;  // recording timer, spec's 58px tier

// Draws a filled rounded pill sized to fit `text` at the given text size,
// right-edge-anchored at (rightX, y), with `fillColor` background and
// `textColor` foreground. Returns the pill's left edge x (useful for
// laying out anything further left).
inline int16_t drawPill(Adafruit_GFX &tft, const char *text, int16_t rightX,
                         int16_t y, uint16_t fillColor, uint16_t textColor,
                         uint8_t textSize = kSizeHint) {
	int16_t x1, y1;
	uint16_t w, h;
	tft.setFont(nullptr);
	tft.setTextSize(textSize);
	tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

	int16_t padX = 9;
	int16_t padY = 3;
	int16_t pillW = w + padX * 2;
	int16_t pillH = h + padY * 2;
	int16_t left = rightX - pillW;

	tft.fillRoundRect(left, y, pillW, pillH, kPillRadiusY, fillColor);
	tft.setTextColor(textColor);
	tft.setCursor(left + padX, y + padY);
	tft.print(text);

	return left;
}

// Draws a device icon chip: a rounded square in `fillColor` with a
// 2-letter `abbrev` centered in `textColor`.
inline void drawIconChip(Adafruit_GFX &tft, int16_t x, int16_t y,
                          const char *abbrev, uint16_t fillColor,
                          uint16_t textColor, int16_t size = kIconChip,
                          int16_t radius = kIconChipRadius) {
	tft.fillRoundRect(x, y, size, size, radius, fillColor);
	tft.setFont(nullptr);
	tft.setTextSize(kSizeHint);
	tft.setTextColor(textColor);
	int16_t x1, y1;
	uint16_t w, h;
	tft.getTextBounds(abbrev, 0, 0, &x1, &y1, &w, &h);
	tft.setCursor(x + (size - w) / 2, y + (size - h) / 2);
	tft.print(abbrev);
}

// Draws one row of a selectable list (the Rigs / Settings screens): a
// full-width rounded plate in `kOkFill` for the active item,
// plain inactive-colored text otherwise — the spec's "active = green
// gradient plate" language, flattened to a solid fill since Adafruit_GFX
// has no cheap gradient fill (same simplification `drawTopBar` uses for
// the vignette). Returns the row's height so callers can stack rows
// `kRowGap` apart without recomputing text metrics themselves.
inline int16_t drawListItem(Adafruit_GFX &tft, const char *text, int16_t x, int16_t y,
                             int16_t width, bool active) {
	tft.setFont(active ? &FreeSansBold9pt7b : &FreeSans9pt7b);
	tft.setTextSize(1);
	int16_t x1, y1;
	uint16_t w, h;
	tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

	const int16_t padX = 12;
	const int16_t itemH = 34;

	if (active) {
		tft.fillRoundRect(x, y, width, itemH, kListItemRadius, kOkFill);
		tft.setTextColor(kOkText);
	} else {
		tft.setTextColor(kTextInactive);
	}
	tft.setCursor(x + padX, y + (itemH - (int16_t)h) / 2 - y1);
	tft.print(text);
	tft.setFont(nullptr);

	return itemH;
}

// Flat colored bar under the header row — the fallback the spec itself
// sanctions in place of a radial-gradient vignette on graphics libraries
// without cheap gradients.
inline void drawTopBar(Adafruit_GFX &tft, uint16_t color) {
	tft.fillRoundRect(kCornerSafeInset, 2, tft.width() - 2 * kCornerSafeInset,
	                  kTopBarHeight, 2, color);
}

} // namespace theme
