# Screen design

Code-facing distillation of the design handoff archived in
`docs/design/` (`screen-handoff-readme.md` is the full spec with mockup
rationale; this file only keeps what `src/theme.h` and `src/menu.cpp`
actually implement from it, in target-panel px, not mockup px).

## Authoritative UI/UX spec: `docs/design/ux-redesign.md` + the pixel mock

As of **2026-08-27** the authoritative source for screen *model* and *flow*
is `docs/design/ux-redesign.md` (built from real shooting experience), and
for pixel layout the mock bundle `docs/design/ux-redesign-mock/`
(`new-model.dc.html` — 24 screens at 280×240 in this firmware's own
palette; `current-screens.dc.html` — the pre-redesign 9). They override the
old handoff screen list and `filming-remote-requirements-v14.md` §3
("Профили") where they disagree. The handoff's **colour system, typography,
and geometry** still stand.

**Direction, in one line:** the home screen is *Configurations*, not the
device list. You pick a shooting rig (Main + Secondary devices), the remote
auto-connects only that rig's devices, and opens straight into Main-centric
control with a compact REC block. Device List demotes to a diagnostics
screen under *Settings › Devices*.

### Implemented — full mock, branch `ux-redesign-rigs`

Copy is **English placeholder** (`CONFIGS`, `OK - REC`, `RUN >`) — the mock
is Russian and a Cyrillic `GFXfont` is a tracked follow-up (see the
"Cyrillic text" section below). Not flashed / hardware-tested; layout is
close to the mock but not pixel-verified.

Code is split: `src/menu.cpp` (state machine + input), `src/menu_render.cpp`
(all `render*`), `src/menu_internal.h` (shared rig/session helpers).

| Mock screen | Firmware |
|---|---|
| 1 / 16 / 17 Configurations | `Screen::Rigs` / `renderRigs()` — 3-row window over `rigStore`, green "last-used" plate with `LAST` tag + `SL + PH` composition, scrollbar, empty-state. Ok launches, `>` → RigMenu, hold-`<` → Settings |
| 2 / 3 Connecting | `Screen::Connecting` / `renderConnecting()` — `MAIN` row boxed + separated from `SECONDARY`/`CAMERAS`, progress bar, 25 s → `NO RESPONSE` + retry. `Menu::update()` auto-advances to Control when Main (or, Main-less, a phone) links |
| 4 / 5 / 8 Control | `renderControlMotion()` — **one fixed mapping for every motion Main** (2026-08-28, Denis: it was over-engineered): `↑↓` = speed, `←→` = drive that direction (same key again or `OK` = stop, opposite = reverse). No axis-binding setting. Screen: identity chip, plain state word (`STOPPED`/`FWD`/`BACK`/`RUNNING`/`HOMING`/`ERROR` — device telemetry wins over the locally-tracked jog state), one big speed numeral (percent of full scale, `Device::speedPercent()` — 2026-08-29, Denis: was a 1..8 level, but the slider's native unit is 1..100% and the remote's own 8-step scale fought the slider's 10Hz speed echo; arrows now step 10% and every device reports a percent), one big direction arrow (pause bars stopped), 8-seg bar (fill = `speedPercent()` rounded to eighths), one action block: `OK - REC` (`n CAM`) with a phone / `OK - START`\|`STOP` for a program device with no phone / plain `OK - STOP` otherwise / `HOLD OK TO CLEAR ERROR` on `inFault()` (long-Ok → `Command::ResetFault`). Per-device `DeviceRegistry::invertDir()` (device card "Flip L/R") swaps `←`/`→` — an `F`/`B` protocol is motor-relative. |
| 6 / 7 Take | same screen, red timer block (`MM:SS`, `* REC`, `n/m CAM`) + `! A CAMERA LOST LINK` yellow strip; Main controls stay live |
| 9 Cameras-only | `renderControlCamerasOnly()` — one row per connected phone (`Phone 1..n`) + a `LINKING` row while waiting, `CAM n/m` in the header, big `OK - REC` box / centred timer |
| 10 Main lost | `renderMainLost()` (overlay on Control, Main loss **outside** a take only); `Menu::update()` auto-fires E-Stop + StopRecord if the link drops mid-take (§4) |
| 11–13 Rig editor | `Screen::Editor` — 4 steps: name (→ TextEntry), Main (motion devices + None), Secondary (camera checkboxes, `>` toggles), take mode. `saveEditor()` → `rigStore` |
| 14 Settings | `renderSettings()` — Configs / Devices / Control / Screen / System, brightness mini-bar on the Screen row |
| 15 / 21 Devices + card | `Screen::Devices` / `renderDevices()` list (alias, kind, coarse "seen") — now 4 devices incl. turntable; `Screen::DeviceCard` — ID, last-seen, `IN RIGS n` / `N bond / M up` for phones, Test link (toggles activation), Rename, `Flip L/R` (motion devices) |
| 23 System / OTA | OTA with no `src/wifi_env.h` → `State::NoWifi` (`NO WIFI CONFIGURED`), not a fake timeout. During upload: `NN%` + progress bar, repainted from inside the blocking `WebServer::handleClient()` via `Ota::setProgressCallback` → `Menu::renderOtaProgress()` |
| 24 Turntable | `TurntableDevice` (index 3, `ali-turn-table-upgrade`, `7ab1e001` service) is a plain Motion device — the generic Control screen drives it. Seed rigs `Turntable + Phone`, `Turntable`. `matchesAdvertisement` matches the service UUID or the name |
| 18 Rig menu | `Screen::RigMenu` — Edit / Duplicate / Rename / Delete (`rigStore` CRUD) |
| 19 Name entry | `Screen::TextEntry` — block cursor + letter ribbon, 5-button; shared by editor step 1, rig rename, device rename |
| 20 Scan | `Screen::Scan` — one-shot `NimBLEScan` (guarded: only with no live session), lists name + RSSI. "Add to registry" is a no-op stub — dynamic drivers are a later phase |
| 22 Control prefs | `Screen::ControlPrefs` — Axes toggle, Max speed (`settings.maxSpeedPercent()` cap, 10..100% in 10% steps), Autostart last, Key sound; `←→` changes value |
| 23 System | `Screen::System` — firmware, battery V, OTA (confirm modal), Factory reset (`settings.factoryReset()` + `ESP.restart()`) |
| OTA active | `renderOtaActive()` — real STA-mode SSID/IP, not the mock's fixed AP |
| 24 Turntable | not built — no turntable driver yet |

**New data model:** `src/rig.h` — `Rig { name[21], mainIndex, secondary[4],
secondaryCount, takeMode }`, mutable. `src/rig_store.*` — NVS blob
(versioned), seeds ux-redesign.md §5 defaults, CRUD for the editor.
`src/device_table.*` — the stable `deviceAt()/deviceCount()/deviceIndexOf()`
identity used by rigs. `src/device_registry.*` — per-device alias (NVS) +
coarse `SeenState`. `Device::kind()` / `supportsHome()` / `speedPercent()`.
`src/settings.*` gains `axisBinding`, `autostartLastRig`,
`maxSpeedPercent`, `buttonSound`; old `profile`/`binding` NVS keys abandoned,
`maxspd` key renamed to `maxspdpct`.

**Slider program API (2026-08-28):** migrated to the slider repo's
`docs/11_program_api.md` model — the pult drives the **Ping-Pong program**
(select on connect, START/STOP, CONFIGURE speed%, confirmed state from
notify), never faked F/B. `Rig` take: `RecordAndMoveMain` sends
`StartProgram`/`StopProgram` for a program-capable Main (else legacy
`MoveForward`/`StopMove`). Slider rigs now default to `RecordAndMoveMain`
(`rig_store` layout bumped to v2 → reseed). Manual jog / go-to-pos / home
calibration / current belong on a future **Advanced/Service** screen (not
built) — on entry it must send Program `STOP`, on exit `SELECT`.

**Multiple phones (2026-08-28):** `PhoneDevice` is now multi-peer — the
one phone slot holds up to `kMaxPhones` (3) bonded phones connected at
once; `_consumerInput->notify()` fans a shutter report to all subscribers
in one call, so `REC`/`STOP REC` hit every phone. `Device::cameraReady()`/
`cameraTotal()` drive the `CAM n/m` counts; `PhoneDevice::markTakeStart()`
holds `total` at the take's peak so a phone dropping mid-take reads `1/2`.
`platformio.ini`: `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=4` (slider + dolly + 2
phones). The old single-peer "quick switch" is removed. **HID-over-GATT to
multiple hosts is an unproven path — bench-test an iPhone + Android both
holding the link and triggering.**

**Two-stage idle (2026-08-28, `main.cpp` + `config.h`):** stage 1 —
`IDLE_SCREEN_OFF_MS` (30 s) of no button activity → backlight off,
rendering paused, BLE/`menu.update()` keep running; any button wakes the
screen and that first press is consumed. Stage 2 — `IDLE_DEEPSLEEP_MS`
(20 min) → `enterDeepSleep()` (full reboot on wake, links auto-reconnect),
suppressed while `menu.takeActive()`.

**Deferred:** an Advanced/Service screen for the slider (manual jog,
go-to-position, current, home calibration); real device registration from
the Scan screen (needs dynamic drivers); turntable driver; a dedicated
battery/charge screen; Cyrillic font.

**Still in force from `requirements-v14`:** emergency stop = long-Ok →
broadcast stop (§9); device-side motion watchdog ~2 s (§9); phone record
indication is about the *take* / command delivery, never a
confirmed-recording claim (§2).

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

Selection on the `Devices` diagnostics screen (Up/Down + Ok-to-toggle) is
shown as a 3px left accent bar in the row's identity color, not a `"> "`
text cursor. The `Rigs` and `Settings` lists use the full-width green
plate from `theme::drawListItem()` for the highlighted row instead.
