# Handoff: Screen UI for Filming Remote (ESP32 handheld)

## Overview

UI for the on-device screen of a handheld filming remote. The remote pairs over BLE with up to
3 devices simultaneously (phone, camera slider, dolly/cart, product turntable) and triggers
recording + motion via a 5-button D-pad (↑ ↓ ← → ●). The screen is the only feedback surface:
no touch, no second display.

This bundle covers 7 screen states across 5 screens:

| Screen | States documented |
|---|---|
| Main / device status | idle (all connected), recording, connection lost |
| Profile select | list with active item |
| Binding presets | list with active preset |
| Menu / settings | list with active item |
| OTA mode | exclusive mode, awaiting connection |

## About the Design Files

`Remote Screen.dc.html` in this bundle is a **design reference created in HTML** — a prototype
showing intended look, hierarchy, and content, not production code to copy.

The target here is **embedded firmware**, not a web app. The implementation task is to recreate
these layouts on the device's actual graphics stack — most likely LVGL, TFT_eSPI, or
u8g2 on an ESP32 driving the Waveshare 1.5" IPS panel. Treat the HTML as a spec for
geometry, color, type scale, and copy; translate to the graphics library's primitives
(draw_rect / labels / styles) using its idioms. Do not attempt to run a browser on the device.

**Critical scaling note:** the HTML mockups are drawn at **580×500 CSS px** for reviewability.
The real panel is **240×280 physical pixels**. Every dimension in this document is given in
BOTH mockup px and target px. Use the *target* column when implementing. The scale factor is
**0.414× horizontally (240/580)** and **0.56× vertically (280/500)** — note the mockups are not
uniformly scaled, so use the per-value target numbers below rather than multiplying yourself.

## Fidelity

**High-fidelity.** Colors, type scale, spacing, and copy are final-intent. The layouts have been
deliberately tuned for a 1.5" screen: max 3 content rows per screen, minimum ~11px effective
text on the real panel, large numerals for glanceable values. Preserve the density decisions —
do not add rows, secondary labels, or decorative chrome, and do not shrink text to fit more in.

## Hardware Constraints That Drove the Design

- **Panel:** Waveshare 1.5" IPS, 240×280, landscape orientation, ~29×25 mm active area
- **Input:** 5 buttons only (↑ ↓ ← → ●). No touch. Every screen must be operable with those 5.
- **Viewing conditions:** on set, often dim, often glanced at mid-shot at arm's length while
  the operator's attention is on the camera. Hence: dark background, high-contrast fills,
  color-as-identity, no thin hairline type.
- **Color budget:** the panel is full-color IPS, and the design uses that — but semantically.
  Color always encodes either *which device* or *what state*, never decoration.

## Color System

Two orthogonal color axes. Do not mix them.

### Axis 1 — Device identity (hue = which device)

| Device | Abbrev | Hue | Fill (oklch) | Approx hex | Text on fill |
|---|---|---|---|---|---|
| Phone | ТЛ | 220 (blue) | `oklch(58% 0.19 220)` | `#0a7fd4` | `oklch(98% 0.02 220)` ≈ `#f4fbff` |
| Slider | СЛ | 140 (green) | `oklch(60% 0.19 140)` | `#00944a` | `oklch(15% 0.03 140)` ≈ `#04180d` |
| Dolly/cart | ТЖ | 300 (violet) | `oklch(58% 0.19 300)` | `#a04ed6` | `oklch(98% 0.02 300)` ≈ `#fdf5ff` |

Device hue appears in: the 54×54 icon chip on the main screen, the D-pad button that controls
that device, and the large icon chip on error screens.

### Axis 2 — State (hue = what's happening)

| State | Hue | Fill | Approx hex | Text on fill |
|---|---|---|---|---|
| OK / ready / moving | 140 green | `oklch(70% 0.19 140)` | `#00b45b` | `oklch(15% 0.03 140)` |
| Recording | 25 red | `oklch(55% 0.2 25)` | `#c8391f` | `oklch(98% 0.02 25)` |
| Warning / no link / OTA | 80 amber | `oklch(75% 0.17 80)` | `#d99a00` | `oklch(18% 0.04 80)` |

Note green appears on both axes — slider identity and OK state share hue 140. This is
intentional and has not caused confusion in review, but if it does, shift slider identity to
hue 175 (teal) and keep OK at 140.

### Neutrals

| Role | Value | Approx hex |
|---|---|---|
| Screen base background | `oklch(13% 0.006 250)` | `#0d0e10` |
| Screen inner border | `oklch(30% 0.01 250)` | `#3a3c40` |
| Row divider | `oklch(24% 0.008 250)` | `#2b2d30` |
| Primary text | `oklch(94% 0.006 250)` | `#ebecee` |
| Secondary text | `oklch(60% 0.008 250)` | `#8b8d91` |
| Hint / footer text | `oklch(45% 0.008 250)` | `#5f6165` |
| Inactive list item text | `oklch(68-70% 0.008 250)` | `#a4a6aa` |

### Contextual screen vignette

Each screen state tints its background with a radial gradient from the top (or bottom for OTA),
fading to the neutral base by 70%:

| Screen state | Vignette |
|---|---|
| Main / idle | `radial-gradient(ellipse 140% 100% at 50% 0%, oklch(22% 0.03 220), oklch(13% 0.006 250) 70%)` |
| Main / recording | `radial-gradient(ellipse 140% 100% at 50% 0%, oklch(24% 0.09 25), oklch(13% 0.006 250) 70%)` |
| Main / no link | `radial-gradient(ellipse 140% 100% at 50% 0%, oklch(26% 0.07 80), oklch(13% 0.006 250) 70%)` |
| OTA | `radial-gradient(ellipse 140% 100% at 50% 100%, oklch(26% 0.07 80), oklch(13% 0.006 250) 70%)` |
| Profile / preset / menu | none (flat `oklch(13% 0.006 250)`) |

Recording and no-link screens also get a colored 1px inner ring:
`inset 0 0 0 1px oklch(45% 0.12 25)` (recording), `oklch(45% 0.1 80)` (warning).

**If the graphics library cannot do gradients cheaply:** replace the vignette with a flat
2–4px colored bar across the top of the screen in the same hue. The vignette is atmosphere,
the state color is the signal — keep the signal.

## Typography

Mockups use IBM Plex Sans (labels, device names) and IBM Plex Mono (numerals, status text,
all-caps chrome). On device, substitute the closest available bitmap/vector font pair — a
humanist sans for names and a monospace for numerals. Monospace for numerals matters:
the recording timer must not jitter as digits change.

| Role | Mockup size / weight | Target px on 240×280 | Font |
|---|---|---|---|
| Screen title (all-caps, e.g. `СЛАЙДЕР+ТЛФ`) | 20px / 600 | 11px | Mono |
| Battery percent | 22px / 600 | 12px | Mono |
| Device name (main screen row) | 30px / 600 | 17px | Sans |
| Status badge text | 20px / 700 | 11px | Mono |
| Device icon chip letters | 20px / 700 | 11px | Mono |
| Recording timer | 108px / 700 | 60px | Mono |
| "● ЗАПИСЬ" badge | 20px / 700, tracking .1em | 11px | Mono |
| Error device name | 40px / 600 | 22px | Sans |
| Error state badge | 24px / 700 | 13px | Mono |
| List item (active) | 30–32px / 700 | 17–18px | Sans |
| List item (inactive) | 30–32px / 400 | 17–18px | Sans |
| OTA heading | 34px / 700 | 19px | Mono |
| OTA IP address | 60px / 700 | 33px | Mono |
| Footer hint | 17px / 500 | 9px | Mono |

**Minimum:** nothing below 9px effective on the panel, and 9px is reserved for the footer
hint line only. Body content floor is 11px.

## Geometry

All screens share one frame:

| Property | Mockup | Target |
|---|---|---|
| Screen box | 580×500 | 240×280 |
| Screen padding | 26px vertical, 28px horizontal | 11px / 12px |
| Corner radius (screen) | 22px | 0 (panel is rectangular; radius is bezel illustration only) |
| Device icon chip | 54×54, radius 14 | 24×24, radius 6 |
| Status badge | padding 8×18, radius 20 (pill) | padding 4×8, radius 10 |
| Row gap (main screen, 3 rows) | 16px | 9px |
| List item padding | 18–20px vertical, 22–24px horizontal | 10px / 10px |
| List item radius | 14px | 6px |
| List gap | 12–14px | 7px |

Vertical structure of every screen, top to bottom:

1. **Header row** (fixed, ~11px target text): screen title left, battery + link dot right.
2. **Content region** (`flex: 1`, vertically centered): max 3 rows, or one large centered value.
3. **Footer hint row** (fixed, ~9px target text, centered, `oklch(45% 0.008 250)`): the button legend.

## Screens

### 1. Main / idle — "all devices ready"

**Purpose:** the default resting screen. Operator glances to confirm everything is paired
before a take.

- Header: `СЛАЙДЕР+ТЛФ` (active profile name, uppercase) left; right side is a 12px green dot
  (`oklch(75% 0.19 140)`) + `82%` battery.
- Content: 3 rows, 16px gap (target 9px). Each row is `space-between`:
  - Left group (16px gap): device icon chip in device hue with 2-letter abbrev + device name at
    30px/600.
  - Right: green `ГОТОВ` pill badge.
  - Rows in order: Телефон (blue chip), Слайдер (green chip), Тележка (violet chip).
- Footer: `● ЗАПИСЬ · ↑↓ СЛАЙДЕР`
- Vignette: blue.

### 2. Main / recording

**Purpose:** confirm recording is live and how long it's been running, readable at a glance
from across a room.

- Header: same structure; link dot is red `oklch(65% 0.19 25)`, battery `79%`.
- Content: vertically centered stack, 14px gap (target 8px):
  - Red pill badge `● ЗАПИСЬ`, tracking .1em.
  - Timer `00:47` at 108px/700 (target 60px), `oklch(96% 0.02 25)`, `line-height: 1`.
  - Green pill badge `СЛАЙДЕР: ДВИЖЕНИЕ` (only shown when a motion device is actually moving;
    omit the badge entirely when nothing is in motion — do not show an idle placeholder).
- Footer: `● — СТОП` in `oklch(70% 0.15 25)`.
- Vignette: red. Inner ring: red.

**Behavior:** the timer counts up in `MM:SS` from record start. Past 59:59 it should roll to
`H:MM:SS` — at that point drop the font to ~44px target so it still fits the 240px width.

### 3. Main / connection lost

**Purpose:** name the specific device that dropped, and reassure that the others are fine.
Fires on BLE link loss.

- Header: profile name left, `85%` right, no link dot.
- Content: vertically centered stack, 12px gap (target 7px):
  - 64×64 (target 28×28) icon chip in the *lost device's* hue, radius 14 (target 6), 2-letter abbrev.
  - Device name at 40px/600 (target 22px), centered.
  - Amber pill badge `⚠ НЕТ СВЯЗИ` at 24px/700.
  - `↻ переподключение…` at 22px/500 in `oklch(50% 0.01 250)`.
- Footer: `телефон, слайдер — ок` — the still-connected devices, comma-separated, lowercase.
  Generate this from the live connection list; if *all* devices are lost, replace with
  `нет связи ни с одним устройством`.
- Vignette: amber. Inner ring: amber.

**Behavior:** screen appears immediately on link loss, and auto-dismisses back to the main
idle screen when the link is restored. Reconnect attempts are automatic — the `↻` line is
status, not a prompt. If reconnect fails past a threshold (suggest 30s), keep the screen up but
change the line to `не удалось · ● повторить` and make ● retry.

### 4. Profile select

**Purpose:** switch which device combination the remote is driving.

- Header: `ПРОФИЛИ` at 20px/600, tracking .08em, `oklch(55% 0.008 250)`, 14px bottom margin.
- Content: vertically centered list, 14px gap (target 7px). Max 3 visible items.
  - Active item: full-width green gradient plate
    `linear-gradient(135deg, oklch(72% 0.19 140), oklch(60% 0.19 160))`, radius 14,
    padding 20×24, text 32px/700 in `oklch(14% 0.03 140)`.
  - Inactive items: no background, padding 20×24, text 32px/400 in `oklch(70% 0.008 250)`.
  - Sample content: `Слайдер+ТЛФ` (active), `Столик+ТЛФ`, `Интервью`.
- Footer: `↑↓ выбор · ● открыть`

**Behavior:** ↑↓ moves the selection (the gradient plate follows it — selection and activation
are the same visual, since committing happens on ●). With more than 3 profiles, scroll the
window so the selected item stays in the middle slot. ← returns to the main screen without
changing the profile.

### 5. Binding presets

**Purpose:** pick which button-to-action mapping is live for the current profile.

Structurally identical to Profile select.

- Header: `БИНДИНГИ`, tracking .05em.
- Content: 3 items, 12px gap. Active item gets the same green gradient plate at padding 18×22,
  text 30px/700. Inactive at 30px/400 in `oklch(68% 0.008 250)`.
  - Sample content: `Слайдер+телефон` (active), `Только слайдер`, `Только запись`.
- Footer: `↑↓ пресет · ● применить`

**Note:** the spec allows free-form binding editing, but that happens in the web config UI over
Wi-Fi, not on this screen. On-device is preset selection only. Do not build an editor here.

### 6. Menu / settings

Structurally identical to the two list screens above.

- Header: `МЕНЮ`, tracking .08em.
- Content: `Профили` (active, green gradient plate), `Яркость`, `OTA-обновление`.
- Footer: `↑↓ · ● · ◀ назад`

**Behavior:** `Яркость` should open an inline value adjuster rather than a submenu — ←→ steps
brightness, ● confirms. `OTA-обновление` requires a confirm step before entering OTA (it kills
BLE), so route it through a yes/no rather than entering directly.

### 7. OTA mode (exclusive)

**Purpose:** firmware update over Wi-Fi. This mode is exclusive — BLE is off, no remote
functions work — so the screen must make that unmistakable.

- Header row: `BLE: OFF` left in `oklch(45% 0.008 250)`, `WI-FI: ON` right in amber
  `oklch(78% 0.15 80)`, both 18px/600, tracking .06em.
- Content: vertically centered, 8px gap:
  - `OTA РЕЖИМ` at 34px/700, tracking .05em, `oklch(92% 0.01 250)`.
  - IP `192.168.4.1` at 60px/700 (target 33px) in amber `oklch(85% 0.14 80)` — this is the
    number the operator types into a laptop, so it's the largest element.
  - `Ожидание подключения…` at 22px/400 in `oklch(48% 0.008 250)`.
- Full-width amber block above the footer: `ФУНКЦИИ НЕДОСТУПНЫ`, background
  `oklch(72% 0.17 80)`, text `oklch(18% 0.04 80)` at 20px/700, radius 12, padding 14×12.
- Footer: `● — выход`
- Vignette: amber, anchored bottom.
- D-pad illustration: all direction buttons rendered dimmed (`oklch(30% 0.008 250)` fill,
  `oklch(35% 0.008 250)` glyph), only ● at normal brightness — reinforcing that only exit works.

**Behavior:** the IP and SSID are live values, not hardcoded. Once a client connects, replace
`Ожидание подключения…` with upload progress: same position, a percentage at the same 22px, and
an amber progress bar. On completion show a success state and reboot. Never leave OTA silently.

## Interactions & Behavior Summary

The remote has 5 inputs. Global rules:

| Input | Main screen | List screens | OTA |
|---|---|---|---|
| ● | start/stop recording | commit selection | exit OTA |
| ↑ / ↓ | control bound motion device (slider) | move selection | — |
| ← / → | control bound motion device (dolly) | ← back, → into submenu | — |
| ● long-press | open menu | — | — |

- **No animation budget assumed.** Screen transitions are instant cuts. If the graphics library
  makes a cheap crossfade available, ≤120ms is acceptable, but nothing is designed to depend on it.
- **The recording indicator should not blink.** Blinking reads as "problem" on set, and the red
  vignette plus timer already carry the state unambiguously.
- **Interrupt priority:** connection-lost overrides whatever screen is showing, except OTA
  (where BLE is off by definition) and except while recording — during recording, show the loss
  as an amber-tinted device row rather than taking over the screen, so the timer stays visible.
  This case is not mocked; it needs a design pass before implementation.

## State Model

```
mode:            IDLE | RECORDING | LIST | OTA
activeProfile:   { name, boundDevices[] }
devices[]:       { id, kind, abbrev, hue, linkState, motionState }
                   linkState:   CONNECTED | LOST | RECONNECTING
                   motionState: IDLE | MOVING
battery:         { percent, charging }
recordStartedAt: timestamp | null
listContext:     PROFILES | PRESETS | MENU  (+ selectedIndex, scrollOffset)
ota:             { active, ssid, ip, clientConnected, progressPercent }
```

Screen selection derives from `mode` plus the highest-priority interrupt. Keep screen choice a
pure function of state — do not drive it from button handlers directly, or the interrupt
priority rules above become impossible to hold.

## Not Yet Designed

Flagging these so they aren't assumed complete:

- Low battery warning state (not mocked)
- Charging state indicator
- Sleep / dimmed screen
- Emergency stop confirmation
- Connection loss *during* recording (behavior described above, no mock)
- Device pairing / first-run flow
- Product turntable (`Предметный столик`) as a device — it's in the hardware spec but was left
  out of these mocks, which cover phone / slider / dolly. It needs an identity hue; suggest 45
  (orange) as the remaining distinct option.

## Assets

None. No images, no icon fonts, no external files. Device icons are 2-letter Cyrillic
abbreviations on colored chips — deliberately, so nothing needs to be drawn or bundled into
firmware. Arrow glyphs (▲ ▼ ◀ ▶ ●) and `⚠ ↻` are Unicode; if the device font lacks them,
draw them as primitives rather than substituting ASCII.

## Copy

All UI copy is Russian and is final — implement verbatim, including abbreviations
(`ТЛФ`, `ТЛ`, `СЛ`, `ТЖ`) and casing. The abbreviations are load-bearing: they fit the 240px
width where full words don't.

## Files

- `Remote Screen.dc.html` — all 7 screen states, laid out for side-by-side review. Open in a
  browser. Each mockup includes a bezel + D-pad illustration around the screen; only the inner
  580×500 dark rectangle is the actual screen. The header of the page also shows a
  true-29×25mm rectangle for size reference.
