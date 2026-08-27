#pragma once

// ---------------------------------------------------------------------------
// Board: Seeed XIAO ESP32S3
// Display: 240x280 SPI IPS, rounded corners, Waveshare 1.5" 240x280 module.
//          Controller is NV3030B (NOT ST7789 — corrected in requirements
//          v10 §6). The code still drives it via Adafruit_ST7789 as a
//          placeholder; NV3030B/LovyanGFX compatibility is an open risk
//          flagged in requirements §6/§12, unresolved until it's checked
//          on a breadboard.
//
// Pin map confirmed pin-by-pin against the actual as-soldered board —
// this took several rounds to nail down (DIN and CLK both initially got
// misreported against pins already claimed elsewhere). All 11 of XIAO
// ESP32S3's broken-out pins (D0..D10 / GPIO1,2,3,4,5,6,43,44,7,8,9) are
// spoken for, zero spare.
// ---------------------------------------------------------------------------

// --- Display (SPI) ---
// Base is the confirmed-working bring-up wiring; moving one pin off
// GND/floating onto a real GPIO at a time and retesting after each,
// to isolate which one (if any) is actually the problem — see
// docs/display-wiring-attempts.md.
#define PIN_TFT_MOSI   9   // D10
#define PIN_TFT_SCK    7   // D8
// Step 1 (confirmed working): CS moved from GND onto D0.
#define PIN_TFT_CS     1   // D0
#define PIN_TFT_DC     43  // D6
// Step 3 (confirmed working): BL moved from 3V3 onto a real GPIO (D2),
// PWM-dimmable via analogWrite.
#define PIN_TFT_BL     3   // D2
// Step 4: RST moved off D1 (freeing it) onto the PCF8575 (P10) instead —
// see PCF_BIT_DISP_RST below and Display::begin(). Testing this now that
// CS/DC/SCK/MOSI/BL are all individually confirmed working, unlike the
// earlier attempt where several things were still unverified at once.
#define PIN_TFT_RST    -1

#define TFT_WIDTH      240
#define TFT_HEIGHT     280
// ST7789 controller RAM is 240x320; Adafruit_ST7789::init(240, 280) auto-
// centers the panel within it (rowstart=20, colstart=0) — no manual
// offset needed here. Recheck once/if the driver is swapped for NV3030B.

// --- Port expander: PCF8575 (16-bit I2C I/O expander) ---
// Up/Down/Left/Right + both status LEDs + display RST all live here —
// only SDA/SCL/INT cost XIAO GPIOs for all 7 of those lines. INT is now
// wired to a real GPIO (v14 — was reserved-but-unused in v10) and drives
// an actual interrupt (see pcf8575.cpp): buttons.cpp only re-reads the
// port over I2C when the expander signals a change, instead of every
// loop tick.
#define PCF8575_ADDRESS  0x20
#define PIN_I2C_SDA      5   // D4
#define PIN_I2C_SCL      6   // D5
#define PIN_PCF_INT      8   // D9

// Bit order matches the physical soldering in v14 §11 (not alphabetical —
// this board was wired before this order was written down):
#define PCF_BIT_BTN_DOWN   0   // P00
#define PCF_BIT_BTN_LEFT   1   // P01
#define PCF_BIT_BTN_RIGHT  2   // P02
#define PCF_BIT_BTN_UP     3   // P03
// Swapped vs. the original P04/P05 assignment below — physically wired
// the other way round on the board (confirmed on hardware: the "yellow"
// channel was blinking with activity/connect semantics, not battery).
#define PCF_BIT_LED_YELLOW 5   // P05
#define PCF_BIT_LED_BLUE   4   // P04
#define PCF_BIT_DISP_RST   8   // P10
// Free on the expander: P06, P07, P11-P17.

// --- Center/Ok button: dedicated GPIO, not on the expander ---
// Must be RTC-capable for esp_sleep_enable_ext0_wakeup() — the PCF8575
// can't identify which input changed until the MCU is already awake and
// reads it over I2C, so it can't serve as a deep-sleep wake source.
// No external pull-up needed — ESP32's internal one, enabled by
// esp_sleep_enable_ext0_wakeup(), is enough for short in-enclosure wiring.
#define PIN_BTN_OK     4   // D3 (GPIO4, RTC-capable, not strapping/UART)

#define BUTTON_DEBOUNCE_MS   30
#define BUTTON_LONGPRESS_MS  600

// --- Battery (2P Li-Po via resistor divider to ADC) ---
// Moved to D1 (GPIO2) — a real ADC1-capable pin — now that RST no longer
// needs it (RST lives on the PCF8575, see above). Fixes the long-standing
// bug where this sat on D7/GPIO44, which has no ADC channel at all
// (ESP32-S3 ADC1 only covers GPIO1-10, ADC2 covers GPIO11-20; GPIO43/44
// are plain digital/UART0 pins).
#define PIN_BATT_ADC   2   // D1
// Divider is 2x 1MOhm (not a low-value pair) to cut standing current to
// ~1.85uA, plus a 100nF-1uF settling capacitor at the ADC pin to counter
// the divider's high impedance — both hardware-only, ratio math below is
// unaffected (still equal resistors).
#define BATT_DIVIDER_RATIO 2.0f
#define BATT_EMPTY_V   3.30f
#define BATT_FULL_V    4.20f

// --- Buzzer: headphone speaker driven via NPN transistor ---
// Physically on D7 (GPIO44) — wasn't actually moved back to D2 when the
// BL/buzzer pin swap was reverted in code, only the #define was. See
// docs/hardware.md for the transistor + resistor values.
#define PIN_BUZZER     44  // D7

// --- Power management (deep sleep, v14 §7 stage 2) ---
// Stage 1 (light sleep, BLE-coordinated) is NOT implemented — needs
// ESP-IDF automatic light sleep config that isn't trivial to get right
// under Arduino/PlatformIO; deferred. This is just the deep-sleep stage:
// full reboot on wake, no BLE-connection preservation attempted.
#define IDLE_DEEPSLEEP_MS   (60UL * 1000)  // TEMP: 1 min for testing, restore to 20 min after
// #define IDLE_DEEPSLEEP_MS   (20UL * 60 * 1000)  // 20 min inactivity -> deep sleep
#define DAILY_CHECK_SEC     (24UL * 60 * 60)     // wake once a day to check battery
#define BATT_CRITICAL_PCT   10

// --- Misc ---
#define SERIAL_BAUD    115200
