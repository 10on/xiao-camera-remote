# Screen design

Code-facing distillation of the design handoff archived in
`docs/design/` (`screen-handoff-readme.md` is the full spec with mockup
rationale; this file only keeps what `src/theme.h` and `src/menu.cpp`
actually implement from it, in target-panel px, not mockup px).

## Status: what's implemented vs. designed-but-not-built

The handoff specs 7 screen states. This firmware has all 7 visual states
(`DeviceList`, `Control`, `Settings`/OTA, `ProfileSelect`, `BindingPresets`
— see `src/menu.h`) plus a connection-lost takeover that overlays
`DeviceList`/`Control` rather than being its own `Screen`.

| Spec screen | Status |
|---|---|
| Main / idle | ✅ implemented as `renderDeviceList()` — one row per registered phone/slider/dolly, with the active profile in the header |
| Main / recording | ✅ implemented as `renderRecording()` with a live `MM:SS` / `H:MM:SS` timer; entered by the phone+motion combo action |
| Main / connection lost | ✅ implemented as a full-screen takeover (`Menu::renderConnectionLost()`), except while recording so the timer keeps priority |
| Profile select | ✅ implemented as a three-row scrolling window over the five real profiles in `src/profile.cpp` |
| Binding presets | ✅ implemented as the three real D-pad mappings from `src/profile.cpp` |
| Menu / settings | ✅ implemented as a real 3-item list (`PROFILES`/`BRIGHTNESS`/`OTA UPDATE`) plus an inline brightness adjuster and an OTA-start yes/no confirm (both `Settings` sub-states, not separate screens — see `Menu::SettingsMode`) |
| OTA mode | ✅ implemented, adapted: shows the *real* STA-mode SSID/IP `ota.cpp` gets, not the spec mockup's fixed AP IP `192.168.4.1` (this firmware joins an existing network rather than hosting its own AP) |

Still not designed or built: a dedicated low-battery/charging screen and
the product-turntable device.

**Brightness control (`Яркость`) is implemented**, via `src/settings.h`
(persisted 0–255 value) and `Display::setBrightness()`
(`analogWrite(PIN_TFT_BL, ...)`). This was previously noted here as
hardware-blocked — that was stale: `config.h` shows the backlight was
already repinned from 3V3 onto a PWM-capable GPIO ("BL moved from 3V3
onto a real GPIO (D2), PWM-dimmable via analogWrite"), and
`test_color_bars.cpp` already exercised `analogWrite(PIN_TFT_BL, ...)`
successfully — the repin had already happened, it just hadn't been wired
into the real menu yet.

## Color system

All values below are the spec's "approx hex" columns converted to
RGB565 macros in `src/theme.h`. Two independent axes — never mixed on
the same element:

**Device identity** (hue = which device) — used for icon chip + name
accent:
| Device | Fill | Text-on-fill |
|---|---|---|
| Phone | `#0a7fd4` | `#f4fbff` |
| Slider | `#00944a` | `#04180d` |
| Dolly | `#a04ed6` | `#fdf5ff` |
| Turntable (reserved, not built) | hue 45 (orange) per handoff | — |

**State** (hue = what's happening) — used for status pills:
| State | Fill | Text-on-fill (approx) |
|---|---|---|
| OK / ready | `#00b45b` | `#04180d` (same formula as slider identity — both hue 140) |
| Recording | `#c8391f` | near-white, warm tint |
| Warning / no link / OTA | `#d99a00` | near-black, warm tint |

**Neutrals:**
| Role | Hex |
|---|---|
| Screen background | `#0d0e10` |
| Border | `#3a3c40` |
| Row divider | `#2b2d30` |
| Primary text | `#ebecee` |
| Secondary text | `#8b8d91` |
| Hint / footer text | `#5f6165` |
| Inactive list text | `#a4a6aa` |

**Vignette fallback:** the spec's radial-gradient vignettes are replaced
with a flat 3px colored bar under the header, in the state's hue — this
is the spec's own explicitly-sanctioned fallback for graphics libraries
without cheap gradients ("the vignette is atmosphere, the state color is
the signal — keep the signal"). `Adafruit_GFX` has no cheap gradient
fill, hence the bar.

## Cyrillic text

The handoff's copy is Russian and marked "final — implement verbatim."
`Adafruit_GFX`'s built-in font is Latin/ASCII-only (no Cyrillic glyphs),
and this project has no other font library (`platformio.ini` only pulls
in Adafruit GFX/ST7789, no U8g2 or similar). **Decided:** ship Latin
placeholder copy now (`SL`/`PH`/`DL` abbreviations, `READY`/`OFF`
English status text) rather than either garbling Cyrillic or blocking the
whole restyle on generating a custom font. Swapping in the real Russian
copy later means generating a Cyrillic `GFXfont` (e.g. via Adafruit's
`fontconvert` from IBM Plex Sans/Mono, the spec's named typefaces) and
wiring UTF-8 decoding into the draw calls — tracked as a follow-up, not
done here.

## Typography → Adafruit_GFX fonts

The layout uses Adafruit's proportional `FreeSans`/`FreeSansBold` faces for
device names and lists, plus `FreeMonoBold` for the recording timer and OTA
IP. Small chrome remains the built-in 8px bitmap face. Cyrillic still needs
a UTF-8-capable font layer; the current copy remains Latin placeholders.

| Role | Spec target px | `setTextSize()` |
|---|---|---|
| Header title / battery / badges / footer hint | 9–12px | 1 |
| Device name / list items | 17–18px | `FreeSans*9pt7b` |
| OTA IP address | 30px | `FreeMonoBold18pt7b` |
| Recording timer | 58px | `FreeMonoBold24pt7b` |

## Geometry (target 280×240 landscape panel)

| Element | Target px |
|---|---|
| Screen padding | 10 vertical / 13 horizontal |
| Rounded-glass safe inset (header/footer) | 28 horizontal |
| Icon chip | 26×26, radius 8 |
| Status pill padding | 3×9, radius 10 (reads as a full pill at this height) |
| Row gap | 9 |
| List item padding | 9×12, radius 11 |

Selection on the device list (Up/Down + Ok-to-toggle — not in the
original 7 mocks, since the spec assumes bindings pick devices rather
than a navigable list) is shown as a 3px left accent bar in the row's
identity color, replacing the old `"> "` text cursor.
