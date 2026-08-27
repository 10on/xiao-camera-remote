#pragma once

// ---------------------------------------------------------------------------
// Board: Seeed XIAO ESP32S3
// Display: 240x280 SPI IPS, rounded corners, ST7789V2-family driver
//          (Waveshare 1.5"/1.69" 240x280 module)
//
// Pin assignment is a starting point — adjust to match actual wiring.
// XIAO ESP32S3 exposes D0..D10 (GPIO1,2,3,4,5,6,43,44,7,8,9).
// ---------------------------------------------------------------------------

// --- Display (SPI) ---
// CS and RST are wired, not GPIO — this is the only SPI device on the
// bus, so CS is tied straight to GND (always selected) instead of eating
// a pin, and Adafruit_ST7789 does a software reset (command 0x01) when
// given rst=-1 instead of a hardware reset pin. That frees up D3 and D7
// for the RGB LED / buzzer below. If your module has no onboard pull-up
// on RST, add one (~10k to 3V3) — leaving it truly floating is unsafe.
#define PIN_TFT_MOSI   9   // D10
#define PIN_TFT_SCK    7   // D8
#define PIN_TFT_CS     -1  // tied to GND in hardware
#define PIN_TFT_DC     43  // D6
#define PIN_TFT_RST    -1  // software reset, no wire
// Backlight tied directly to 3V3 on most 240x280 modules; define if wired to a GPIO instead.
// #define PIN_TFT_BL   -1

#define TFT_WIDTH      240
#define TFT_HEIGHT     280
// ST7789 controller RAM is 240x320; Adafruit_ST7789::init(240, 280) auto-
// centers the panel within it (rowstart=20, colstart=0) — no manual
// offset needed here.

// --- Buttons (5x, active LOW, internal pull-up) ---
#define PIN_BTN_UP     1   // D0
#define PIN_BTN_DOWN   2   // D1
#define PIN_BTN_LEFT   5   // D4
#define PIN_BTN_RIGHT  6   // D5
#define PIN_BTN_OK     8   // D9

#define BUTTON_DEBOUNCE_MS   30
#define BUTTON_LONGPRESS_MS  600

// --- Battery (2S... actually 2P Li-Po via resistor divider to ADC) ---
#define PIN_BATT_ADC   3   // D2 (ADC1 channel, adjust to actual divider wiring)
// Voltage divider ratio: if using two equal resistors, VBAT = ADC_V * 2
#define BATT_DIVIDER_RATIO 2.0f
#define BATT_EMPTY_V   3.30f
#define BATT_FULL_V    4.20f

// --- RGB LED (WS2812, single pixel) ---
// Freed up by tying the display's CS to GND (see above).
#define PIN_RGB_LED    4   // D3
#define RGB_LED_COUNT  1
#define RGB_LED_BRIGHTNESS 40 // 0-255, kept low — it's a status indicator, not a light

// --- Buzzer (passive, driven via tone()) ---
// Freed up by using the display's software reset (see above).
#define PIN_BUZZER     44  // D7

// --- Misc ---
#define SERIAL_BAUD    115200
