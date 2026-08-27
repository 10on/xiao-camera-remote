# Hardware

## MCU
Seeed Studio XIAO ESP32S3. All 11 broken-out pins (D0–D10) are spoken for
— see the pin map below. If you need to add anything else, either free a
pin the same way CS/RST were freed (see Display), or add an I2C port
expander (PCF8574, as used in `~/projects/slider`).

## Display
240x280 SPI IPS panel, rounded corners, ST7789V2-family driver
(matches Waveshare's 1.69" 240x280 module — controller RAM is 240x320;
`Adafruit_ST7789::init(240, 280)` auto-centers it, rowstart=20/colstart=0,
no manual offset needed). Rendered landscape (`setRotation(1)` in
`Display::begin()`).

**CS and RST don't use GPIOs.** This is the only SPI device on the bus, so
CS is tied straight to GND in hardware (always selected) instead of a
pin — pass `-1` for it and `Adafruit_ST7789` skips managing it entirely.
RST is left with no wire — pass `-1` and the library does a software
reset (command `0x01`) instead of pulsing a hardware pin. This freed D3
and D7 for the RGB LED and buzzer below. **Caveat:** if your particular
module has no onboard pull-up on RST, add one yourself (~10kΩ to 3V3) —
a truly floating RST is unsafe, most small SPI TFT breakouts already have
this pull-up built in but check before assuming.

Panel resolution/driver family should be double-checked against the
actual module's datasheet once wired up — `Adafruit_ST7789::init()` and
`setRotation()` are the calls to adjust if the picture is shifted,
mirrored, or the wrong way up.

## Buttons
5x momentary, active LOW, wired to internal pull-ups: Up / Down / Left /
Right / Ok (a D-pad). No dedicated Fwd/Back/Stop/Home buttons — those are
abstract `Command`s the active device(s) translate into their own
protocol (see `src/command.h`, `Menu::handleButton` in `src/menu.cpp`).

## Battery
2P Li-Po pack (two cells in parallel — same nominal voltage as a single
cell, ~2x capacity). XIAO ESP32S3 has no built-in voltage-sense pin —
voltage is read through a resistor divider into an ADC pin;
`BATT_DIVIDER_RATIO` in `src/config.h` must match the actual divider
resistors. The onboard charge IC (BAT+/BAT- pads) handles charging over
USB-C independently of this.

## RGB LED (status indicator)
Single WS2812 (NeoPixel) pixel, freed up by the CS-to-GND trick above.
Shows: green = at least one device connected, amber = active device still
connecting, blue = WiFi OTA in progress, off = nothing active. See
`src/led.cpp` / `Menu::updateStatusLed()`.

## Buzzer
Passive piezo, driven via `tone()`/`noTone()`, freed up by the RST
software-reset trick above. Currently used for a short confirmation beep
on every button press (`src/main.cpp`) — the "command registered"
tactile/audio feedback called out in the requirements doc (§13).

## Pin map
See `src/config.h` — every pin is a `#define` there, none hardcoded
elsewhere. All 11 of XIAO ESP32S3's broken-out D0–D10 pins are used;
adjust to match actual wiring before first flash.

| Signal      | XIAO pin | GPIO |
|-------------|----------|------|
| TFT MOSI    | D10      | 9    |
| TFT SCK     | D8       | 7    |
| TFT CS      | — (GND)  | —    |
| TFT DC      | D6       | 43   |
| TFT RST     | — (n/c)  | —    |
| RGB LED     | D3       | 4    |
| Buzzer      | D7       | 44   |
| BTN Up      | D0       | 1    |
| BTN Down    | D1       | 2    |
| BTN Left    | D4       | 5    |
| BTN Right   | D5       | 6    |
| BTN Ok      | D9       | 8    |
| Battery ADC | D2       | 3    |

Backlight is assumed tied directly to 3V3 (no GPIO control — there was
none to spare). If it's wired to a pin instead for PWM dimming, you'll
need to free one up first; define `PIN_TFT_BL` in `config.h` and it'll be
driven HIGH in `Display::begin()` (no PWM dimming logic exists yet).

## Wiring — where to solder what

XIAO ESP32S3 pin labels are the ones silkscreened on the board (`D0`..`D10`,
`3V3`, `GND`, `5V`). Numbers above are for reference only — always solder
to the **label**, not the GPIO number.

### Display (8-pin SPI panel: VCC/GND/DIN/CLK/CS/DC/RST/BL)

| Display pin      | XIAO pin |
|-------------------|----------|
| VCC                | 3V3      |
| GND                | GND      |
| DIN (MOSI/SDA)     | D10      |
| CLK (SCK/SCL)      | D8       |
| CS                 | **GND** (direct, not a XIAO pin) |
| DC (A0/RS)         | D6       |
| RST (RES)          | **not connected** (leave floating only if the module has its own RST pull-up — see caveat above) |
| BL (LED/backlight) | 3V3      |

### RGB LED (WS2812 single pixel: VCC/GND/DIN)

| LED pin | XIAO pin |
|---------|----------|
| VCC     | 3V3      |
| GND     | GND      |
| DIN     | D3       |

### Buzzer (passive piezo, 2 leads, polarity doesn't matter)

| Buzzer lead | XIAO pin |
|-------------|----------|
| Lead 1      | D7       |
| Lead 2      | GND      |

### Buttons (5x momentary, no direction matters)

One leg of every button goes to **GND** (any GND pin — they're all the same
net), the other leg to its own signal pin:

| Button | XIAO pin |
|--------|----------|
| Up     | D0       |
| Down   | D1       |
| Left   | D4       |
| Right  | D5       |
| Ok     | D9       |

No external pull-up resistors needed — firmware enables `INPUT_PULLUP` on
all five.

### Battery (2P Li-Po pack)

1. **Power**: solder the pack's +/- leads to the XIAO's onboard battery
   pads (`BAT+` / `BAT-`, next to the JST connector footprint on the
   underside). The onboard charge IC handles charging over USB-C — no
   extra wiring needed for that part.
2. **Voltage sense** (so the firmware can show battery %): build a divider
   from `BAT+` to `GND` with two equal resistors (e.g. 100kΩ + 100kΩ), and
   tap the **midpoint** into `D2`. That halves the ~3.0–4.2V pack voltage
   into the ESP32S3 ADC's safe range, matching `BATT_DIVIDER_RATIO = 2.0`
   in `config.h`. If you use different resistor values, update that
   constant to match (`ratio = (R1+R2)/R2` where R2 is the resistor to
   GND).

## Bring-up test

Before wiring buttons/LED/buzzer/battery, verify the display alone: build
and flash the `color_bars` PlatformIO env (`make bars-upload`), which
draws 7 vertical color bars (white/yellow/cyan/green/magenta/red/blue)
landscape, using nothing but `display.cpp`. If the bars show correctly —
right colors, right orientation, no smearing — the SPI wiring (VCC/GND/
DIN/CLK/DC, plus CS-to-GND) is correct. Mirrored/shifted/upside-down
picture usually means `setRotation()` needs a different value in
`Display::begin()`.
