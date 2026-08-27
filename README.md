# XIAO Remote (multi-device BLE control)

Battery-powered handheld remote, built around a Seeed XIAO ESP32S3, a
240x280 color SPI display and 5 buttons. It's a BLE **central** that can
hold several BLE connections at once — you activate 2-3 devices for a
shoot from a device list, then one button press fans an abstract command
out to every active device simultaneously.

Full product requirements: `~/Downloads/filming-remote-requirements.md`.

The first supported device is [`slider`](../slider) (the camera slider
controller), which acts as a BLE peripheral named `Camera_Slider`. More
devices get added by implementing the `Device` interface and registering
it in `src/menu.cpp`.

## Hardware
See `docs/hardware.md` for pinout and module notes.

## Screen design
See `docs/screen-design.md` for the color system, typography, and
geometry the UI is built from, and which of the 7 designed screen states
are implemented vs. still blocked on a missing profiles/bindings model or
on hardware (brightness control). Original handoff bundle archived in
`docs/design/`.

## Build
```
make build      # pio run
make upload     # build + flash over USB
make monitor     # serial monitor w/ exception decoder
```

## Display bring-up test
Before wiring buttons/battery, verify the panel and SPI wiring alone with
the color-bars test firmware (draws 7 classic TV color bars, no BLE/
buttons/battery involved):
```
make bars-upload   # build + flash src/test_color_bars.cpp
```

## Structure
```
src/
  config.h              pin map & tunables — the only place hardware pins live
  command.h             abstract Command enum (MoveForward, Record, ...) —
                         the vocabulary buttons speak, independent of any
                         device's actual BLE protocol
  display.h/.cpp         ST7789 panel wrapper
  buttons.h/.cpp          debounced 5-button reader (press / long-press)
  battery.h/.cpp          pack voltage/percent via ADC divider
  ble_manager.h/.cpp      owns the single shared BLE scan, (re)connects
                          every active device, keeps them independent
  ota.h/.cpp              WiFi OTA update, entered from Settings
  led.h/.cpp              single WS2812 status pixel
  buzzer.h/.cpp           passive piezo, non-blocking beep()
  menu.h/.cpp             UI: device list / control / settings screens
  devices/
    device.h              interface every controllable peripheral implements
    slider_device.h/.cpp  BLE driver for Camera_Slider
```

## Architecture
```
[Buttons] → [Menu: DeviceList / Control / Settings]
                       │ Command (fan-out to every *active* device)
                       ▼
              ┌────────┴────────┐
              │  SliderDevice    │  ...more Device drivers later
              └────────┬────────┘
                       │ device-specific BLE GATT calls
                       ▼
                 [BleManager]  — owns the one shared scan, connects/
                                 reconnects every active device
```

A `Device` doesn't scan or hold the shared radio itself — `BleManager`
does that centrally (NimBLE only supports one scan at a time) and calls
`Device::beginGattConnection()` once it spots a matching advertisement.
`Device::activate()/deactivate()` just flip a want-connection flag; the
manager keeps retrying for active-but-disconnected devices and stops
scanning once everything active is connected.

## UI flow
- **Device list** (default screen): Up/Down select a device, **Ok**
  toggles it active/inactive (`[ ]` off, `[.]` active-connecting, `[x]`
  connected), **Right** enters Control, long-**Up** enters Settings.
- **Control**: Right/Left → `MoveForward`/`MoveBackward`, Up/Down →
  `SpeedUp`/`SpeedDown`, Ok (short/long) → `StopMove`/`Home` — sent to
  *every* active device via `Command`. Long-**Left** returns to the list.
- **Settings**: **Ok** starts WiFi OTA (see below). Long-**Left** cancels
  it and returns to the list.

## Adding a new device
1. Create `src/devices/<name>_device.h/.cpp` implementing `Device`:
   `activate()/deactivate()` just set a flag; `beginGattConnection()` does
   the actual client-connect + service/characteristic discovery once
   `BleManager` hands you a matching advertisement; `handleCommand()`
   translates a `Command` into this device's protocol (ignore commands
   that don't apply); `renderStatusLine()` draws one line of status.
2. Add it to `kDevices[]` **and** register it via `bleManager.registerDevice()`
   in `Menu::begin()` (`src/menu.cpp`).
3. Add any new logical actions to `src/command.h` if the existing
   `Command` set doesn't cover them (e.g. phone recording will need
   something beyond `Record`/`StopRecord` if it turns out to need more).

## WiFi OTA
Entered manually from **Settings → Ok**. Connects to the first network in
`src/wifi_env.h` (copy from `src/wifi_env.h.example`, gitignored — same
pattern as `../slider`), then serves `/update` (upload a `.bin`, reboots
on success). Gives up and turns WiFi back off if nothing is uploaded
within 60s of the server coming up, or if it can't join WiFi within 12s.
WiFi is off the rest of the time — it shares the radio with BLE and can
disrupt active device connections.

## Slider commands
`Command::MoveForward/MoveBackward/StopMove/Home` map to the slider's
`F`/`B`/`S`/`H` protocol bytes; `SpeedUp`/`SpeedDown` step its speed
characteristic. Full BLE protocol: `../slider/docs/03_ble_protocol.md`.

## Status indicator (RGB LED) + button feedback
A single WS2812 pixel (`src/led.h/.cpp`) shows connection state without
needing the screen on — green (device connected), amber (connecting),
blue (WiFi OTA active), off (nothing active); see `Menu::updateStatusLed()`.
Freed up the pins by not wiring the display's CS/RST to a GPIO (see
`docs/hardware.md`). A passive buzzer (`src/buzzer.h/.cpp`) gives a short
beep on every button press — confirmation that a blind press registered.

## Known gaps vs. the requirements doc
Requirements doc is now v4 (`docs/design/requirements-v4.md`, mirrored to
`~/Downloads/filming-remote-requirements.md`). Resolved since v3: phone
control is BLE HID (peripheral role, emulates a headset button — pult
must be BLE dual-role, central to slider/dolly and peripheral to phone at
once), a product turntable is now a planned device (recommended to share
a `MotionAxisDriver` family with the slider), and profiles/bindings are
decided-but-not-built (structure in the doc's §3, no NVS storage or
editor yet — see `docs/screen-design.md`).

Not yet implemented: persistent storage of known devices + auto-reconnect
after power-cycle, idle screen-off while keeping BLE alive, emergency
stop, phone/dolly/turntable drivers, profiles/bindings storage and UI,
backlight brightness control (needs a repin, see `docs/hardware.md`).
