# Hardware

Reflects `docs/design/filming-remote-requirements-v14.md` — that's the
source of truth for *decisions* (this is the "распаяно", already-soldered
revision); this doc is the source of truth for *current pin map and code
state*, and should stay in sync with `src/config.h`.

## MCU
Seeed Studio XIAO ESP32S3. All 11 broken-out pins (D0–D10) are spoken for
— see the pin map below, zero pins spare.

## Port expander: PCF8575
16-bit I2C I/O expander. Only 3 XIAO pins go to it (SDA/SCL/INT), yet it
carries the 4 arrow buttons, both status LEDs, *and* the display's RST
line — 7 of its 16 lines used, 9 free for future expansion. This is what
makes the pin budget fit at all (see "Why a port expander" below).

**Buttons on it (quasi-bidirectional, active LOW):** hold the pin at 1 to
read it as an input; the button shorts it to GND when pressed. Internal
pull-up is weak (~100µA) — no external pull-up needed for short wiring
inside one enclosure (v14 dropped the earlier "maybe add 10kΩ" caveat
once the actual wiring turned out to be short).

**LEDs on it:** PCF8575 pins source almost no current but sink several mA
comfortably, so both LEDs are wired **active-LOW**: VCC → resistor → LED
anode, cathode → PCF8575 pin, pin driven LOW lights it. The reverse wiring
(pin as source) gives a barely-visible glow, not a proper "on". No PWM on
this chip either — brightness isn't controllable, only on/off/blink.

**Display RST on it (P10):** see the Display section below — this is the
biggest change from earlier revisions (RST used to be a passive RC
network, now it's an expander bit the firmware must pulse explicitly).

**INT is wired and used.** `src/pcf8575.cpp` attaches a real interrupt on
`PIN_PCF_INT` (falling edge) and only re-reads the port over I2C when it
fires (`Pcf8575::changed()`), instead of polling every loop tick. Writes
this class makes itself (LEDs, RST) can also trigger a spurious INT — the
chip doesn't distinguish "I changed a pin" from "the outside world changed
a pin", it just compares to the last read. Harmless, just one extra I2C
read.

### Why a port expander (not direct GPIO)
XIAO ESP32S3 has 11 usable pins. Display SPI+BL (5) + battery ADC (1) +
center button (1) + buzzer (1) already claims 8 before a single d-pad
button is wired. Putting all 5 buttons directly on GPIOs plus 2 LEDs plus
a display RST would need 8 more pins — doesn't fit. Routing 4 buttons + 2
LEDs + RST through PCF8575 costs only 3 pins (SDA/SCL/INT) for all 7,
which is what makes the budget land at exactly 11/11 with the center
button still on its own GPIO.

## Buttons
5x momentary, active LOW, no direction matters for wiring.

- **Up / Down / Left / Right** — one leg to any GND, other leg to a
  PCF8575 line. Bit order is *not* alphabetical — it follows how the
  board was actually soldered (`PCF_BIT_BTN_*` in `config.h`):
  ↓=P00, ←=P01, →=P02, ↑=P03.
- **Ok (center)** — deliberately **not** on the expander. It's wired
  directly to `PIN_BTN_OK` (D3/GPIO4), a dedicated, RTC-capable XIAO GPIO,
  because it's the intended deep-sleep wake source
  (`esp_sleep_enable_ext0_wakeup`) — the PCF8575 can't identify which of
  its inputs changed until the MCU is already awake and reads it over
  I2C, so it can't wake the MCU on its own. Wired to GND, `INPUT_PULLUP`,
  no external pull-up needed.

No dedicated Fwd/Back/Stop/Home buttons — those are abstract `Command`s
the active device(s) translate into their own protocol (see
`src/command.h`, `Menu::handleButton` in `src/menu.cpp`).

## Battery
2P Li-Po pack (two cells in parallel — same nominal voltage as a single
cell, ~2x capacity). Voltage is read through a resistor divider into
`PIN_BATT_ADC`.

⚠️ **Known bug in the current pin assignment, not yet fixed (deferred on
purpose — see `config.h`):** requirements v14 assigns this to D7, which is
**GPIO44 — not an ADC-capable pin on ESP32-S3** (ADC1 only covers
GPIO1-10, ADC2 covers GPIO11-20; GPIO43/44 are plain digital/UART0 pins).
As wired today, `battery.cpp`'s `analogReadMilliVolts()` call cannot
produce a real reading. Needs moving to a GPIO1-10 pin once there's slack
in the budget to rework it — tracked here, not silently patched around.

`BATT_DIVIDER_RATIO` in `src/config.h` must match the actual divider
resistors — **1MΩ + 1MΩ**, not a low-value pair, to keep standing current
around ~1.85µA instead of the hundreds of µA a 10kΩ-class divider would
cost sitting there permanently. That high impedance needs a settling
capacitor (100nF–1µF, ceramic) right at the ADC pin, or readings come out
noisy — see the passives table below. The onboard charge IC (BAT+/BAT-
pads) handles charging over USB-C independently of this.

## Status LEDs (2x discrete, on PCF8575)
WS2812 was the earlier plan but got dropped — not in stock, not worth a
separate order. Two small 3×5mm LEDs instead:

- **Yellow — power** (P04, 330Ω resistor, ~4-5mA). Solid = on USB,
  blinking = discharging/low, fast blink = critical. (Gap: no
  charge-detect GPIO is wired up yet, so `Menu::updateStatusLed()`
  currently approximates this from battery percent alone.)
- **Blue — workflow** (P05, 100Ω resistor — lower value because this LED's
  higher forward voltage leaves less headroom off 3.3V, ~3-4mA). Solid =
  active (recording/moving/OTA), blinking = waiting/connecting/lost
  connection.

Both resistor values were picked for dim, not max, brightness on purpose
— bright blinking LEDs are a distraction/reflection problem for a
filming tool. Both driven through `StatusLed` (`src/led.h/.cpp`),
active-LOW as described above.

## Display
Waveshare 1.5" 240×280 IPS SPI panel. **Controller is NV3030B**, not
ST7789 (an earlier assumption was wrong — corrected in requirements v10+).
Firmware currently still drives it via `Adafruit_ST7789` as a
placeholder — NV3030B compatibility (register init sequence, LVGL partial
buffers) is an **open risk, not yet validated on hardware**. Don't treat
`display.cpp` as confirmed-working for this exact panel; it needs
breadboard testing before that placeholder can be trusted or replaced.

Rendered landscape (`setRotation(1)` in `Display::begin()`).

**RST moved from a passive RC network (v10) to the PCF8575, bit P10
(v14).** Reasoning: the device does a full `setup()` reboot on every
deep-sleep wake anyway, so a full display reset+reinit on every boot is
free — it removes the need to guess NV3030B's undocumented sleep-in/
sleep-out opcode entirely, since the display is just never put to sleep,
only fully reset. **Firmware-critical:** PCF8575 pins power up HIGH via
their weak internal pull-up, so RST does **not** pass through LOW
automatically at power-on — `Display::begin()` must explicitly pulse it
LOW (~20ms) then HIGH (~120ms settle) before running the SPI init
sequence, on both cold boot and post-deep-sleep reboot. This means
`pcf8575.begin()` must run before `display.begin()` in every entry point
(see `main.cpp` and `test_color_bars.cpp`).

**BL is a direct hardware-PWM (LEDC) GPIO** (`PIN_TFT_BL` = D10/GPIO9),
not routed through the PCF8575 (which can't do PWM) and not driven
through a transistor — confirmed against Waveshare's own ESP32 reference
wiring (BL straight off a GPIO).

## Buzzer
Not a passive piezo — a **headphone speaker driven through an NPN
transistor** (any TO-92 NPN: S8050/2N2222/BC547-type), on `PIN_BUZZER`
(D2/GPIO3). Software side is unchanged: `tone()`/`noTone()` via
`ledcWriteTone()` under the hood — the transistor just switches speaker
current, doesn't change how the GPIO is driven.

D2/GPIO3 is a strapping pin (sets JTAG source at boot) — deliberately
handed to the buzzer since a boot-time glitch here is harmless (at worst
an inaudible click), and every cleaner RTC-capable pin was needed
elsewhere (center button).

```
GPIO (D2/GPIO3) --[R_base 1kΩ]-- Base NPN
                                  Emitter -- GND
Speaker(+) -- 3.3V
Speaker(-) -- [R_limit 150-220Ω] -- Collector NPN
```

The limiting resistor is mandatory, not optional: headphone speaker
coils are typically 16–32Ω, so without it 3.3V would push 100-200mA
through both the coil and the transistor — too much for either. 150–220Ω
keeps it to a sane 10-20mA, plenty loud for a UI beep. No snubber diode
needed (speaker coil inductance is tiny, unlike a relay).

## Pin map
See `src/config.h` — every pin is a `#define` there, none hardcoded
elsewhere. All 11 of XIAO ESP32S3's broken-out D0–D10 pins are used, zero
spare.

| Signal            | XIAO pin | GPIO | Notes |
|-------------------|----------|------|-------|
| TFT DC            | D0       | 1    | |
| TFT CS            | D1       | 2    | |
| Buzzer            | D2       | 3    | strapping pin, deliberately least-critical |
| Center/Ok button  | D3       | 4    | direct GPIO — deep-sleep wake source |
| I2C SDA (PCF8575) | D4       | 5    | |
| I2C SCL (PCF8575) | D5       | 6    | |
| TFT SCK           | D6       | 43   | |
| Battery ADC       | D7       | 44   | ⚠️ not ADC-capable, see Battery section |
| TFT DIN (MOSI)    | D8       | 7    | |
| PCF8575 INT       | D9       | 8    | wired and read via real interrupt |
| TFT BL            | D10      | 9    | direct GPIO, hardware PWM (LEDC) |

**On the PCF8575** (not XIAO pins — see `PCF_BIT_*` in `config.h`):

| Signal      | PCF8575 bit |
|-------------|-------------|
| BTN Down    | P00 |
| BTN Left    | P01 |
| BTN Right   | P02 |
| BTN Up      | P03 |
| LED Yellow  | P04 |
| LED Blue    | P05 |
| TFT RST     | P10 |
| *(free)*    | P06, P07, P11–P17 |

## Passive components
From `docs/design/filming-remote-requirements-v14.md` §13 — everything
that isn't in the pin table above because it's a resistor/capacitor, not
a GPIO. RC-on-RST and button pull-up entries from earlier revisions are
gone — RST moved onto the expander (no RC needed) and internal pull-ups
turned out sufficient for the actual (short, in-enclosure) wiring.

| Component | Value | Where | Why |
|---|---|---|---|
| LED resistor, yellow | 330Ω | VCC → resistor → anode; cathode → P04 | current limiting, lower Vf |
| LED resistor, blue | 100Ω | VCC → resistor → anode; cathode → P05 | current limiting, higher Vf leaves less headroom |
| I2C pull-ups | 4.7kΩ | SDA and SCL | check the PCF8575 breakout doesn't already have these |
| Battery divider | 2× 1MΩ | VBAT → R → ADC pin → R → GND | minimizes standing current |
| Divider settling cap | 100nF–1µF ceramic | ADC pin to GND, close to the pin | required for a clean reading off a 1MΩ+1MΩ divider |
| Buzzer base resistor | 1kΩ | GPIO → transistor base | |
| Buzzer limit resistor | 150–220Ω | in series with the speaker | protects coil + transistor |
| PCF8575 decoupling cap | 100nF ceramic | VCC/GND at the chip | standard practice |
| XIAO buffer cap | 100–470µF electrolytic/tantalum | BAT+/BAT- at XIAO, after the protection board | smooths BLE TX current spikes, avoids brownout reboots |

⚠️ The battery divider's settling cap and the XIAO buffer cap aren't
decorative — without the first, the divider still "works" but lies about
charge level; without the second, expect unexplained reboots during
active BLE traffic (worst possible moment — mid slider move).

## Open gaps (known, not yet implemented)
- **Battery ADC pin is wrong** — see the Battery section above. Deferred
  on purpose; work with the current wiring until there's a reason to
  rework it.
- **Charge-detect input** — yellow LED's "on USB" state is approximated
  from battery percent (`Menu::updateStatusLed()`), not an actual
  charging signal; no GPIO is budgeted for this.
- **Deep sleep / two-stage idle** — not implemented. Center button is
  wired as the intended `ext0` wake source but no sleep logic exists yet.
- **NV3030B display driver validation** — see the Display section above.
- **Profiles / bindings** (requirements v14 §3) — no `Profile` model
  exists in code yet; `Menu` hardcodes a single fixed device list and
  fixed bindings.

## Bring-up test
Before wiring buttons/LED/buzzer/battery, verify the display alone: build
and flash the `color_bars` PlatformIO env (`make bars-upload`), which
draws 7 vertical color bars (white/yellow/cyan/green/magenta/red/blue)
landscape. It now also brings up the PCF8575 first (needed for the RST
pulse — see Display section), so it exercises the I2C link too, not just
SPI. If the bars show correctly — right colors, right orientation, no
smearing — SPI wiring and the I2C/RST path are both correct.
Mirrored/shifted/upside-down usually means `setRotation()` needs a
different value; a blank/garbage screen with I2C otherwise looking fine
usually points at the NV3030B-vs-ST7789 driver risk above.
