# TouchFocus

TouchFocus is an open-source wireless touchscreen handcontroller for an
astronomical INDI focuser. It does not drive the stepper motor directly. The
handcontroller sends commands to `focuserd` running on a Raspberry Pi, while
the daemon remains responsible for the TMC2209, limits, homing and integration
with INDI/KStars/Ekos.

```text
TouchFocus (ESP32-P4 + ESP32-C6)
              |
         BLE or Wi-Fi
              |
        Raspberry Pi 5
              |
           focuserd
              |
           TMC2209
              |
        stepper motor
```

![TouchFocus user interface](Untitled.png)

## Current features

- Portrait 480 x 800 LVGL touchscreen interface
- Nine presets: short press moves, long press (900 ms) saves the current position
- Editable and persistent preset names
- Hold-to-move IN/OUT with fine movement and faster jog after two seconds
- STOP on release and a daemon-side heartbeat watchdog
- HOME control and live position/connection state
- Wi-Fi scan, password entry and persistent credentials
- Automatic `focuserd` discovery on the current Wi-Fi subnet
- Direct BLE connection through a Raspberry Pi BlueZ bridge
- Wi-Fi, BLE, SSID, IP address and signal indicators
- COLOR and red astronomy NIGHT modes
- Configurable display timeout and optional GPIO33 WAKE interlock
- Editable focuser mechanical calibration
- Persistent settings in ESP32 NVS

## Hardware

- Guition `JC4880P443C_I_W` / `JC-ESP32P4-M3`
- ESP32-P4 engineering sample revision v1.3
- ESP32-C6 Wi-Fi/BLE coprocessor
- 16 MB flash and 32 MB QSPI PSRAM
- 4.3-inch 480 x 800 IPS MIPI-DSI display with ST7701S
- GT911 capacitive touch controller
- optional 3.7 V LiPo battery

### Critical ESP32-P4 ES requirement

The firmware **must** be built with:

```text
chip_variant = esp32p4_es
```

Do not replace the bundled board definition with a normal production
`esp32p4` definition. A production build can terminate with `Illegal
instruction` on the v1.3 chip.

The project deliberately pins pioarduino platform-espressif32 to
`55.03.36-1`. Nearby versions have caused boot loops on this ES hardware. Do
not update the platform casually.

## Physical WAKE button

Connect a normally-open momentary button between GPIO33 and GND:

```text
GPIO33 ---- momentary button ---- GND
```

The internal pull-up is enabled. When the WAKE interlock is active, pressing
WAKE turns on the display, touch is accepted only while WAKE remains held, and
releasing it during IN/OUT sends STOP. The interlock can be disabled in Screen
Settings. The first touch after display-off is always consumed as a wake-only
event and cannot activate a motor command.

## User interface navigation

```text
EDIT PRESETS <- MAIN -> SETTINGS -> FOCUSER SETUP -> SCREEN SETTINGS
```

- **MAIN** — P1-P9, IN, OUT, HOME, position and connection state
- **EDIT PRESETS** — rename P1-P9
- **SETTINGS** — Wi-Fi and BLE configuration
- **FOCUSER SETUP** — mechanical calibration stored by `focuserd`
- **SCREEN SETTINGS** — NIGHT/COLOR, display timeout and WAKE interlock

## Building the firmware

The verified setup uses VS Code, PlatformIO Core 6.1.19 and the pinned
pioarduino platform in `platformio.ini`.

```bash
pio run
```

Upload is intentionally separate:

```bash
pio run --target upload
```

Review the selected serial port first. Keep `platformio.ini`,
`boards/jc4880p4.json`, the BSP and pinned platform together.

The display/touch BSP is vendored under `lib/guition-jc4880p4-bsp` from
[ultramcu/guition-jc4880p4-bsp](https://github.com/ultramcu/guition-jc4880p4-bsp).

## Raspberry Pi and focuserd

The daemon listens for JSON commands on UDP port 40000 and broadcasts status on
UDP port 40001. The default Raspberry Pi address is `192.168.88.240`; change it
in `src/config/network_config.h` when needed.

Files in `reference/`:

- `focuserd.py` — daemon with TouchFocus UDP, preset, jog and configuration extensions
- `focuser_ble.py` — BLE GATT-to-UDP bridge; it never accesses GPIO
- `focuser-ble.service` — systemd unit for the BLE bridge
- `focuserd.pyold` — preserved historical backup

The reference daemon uses the existing Raspberry Pi wiring and `gpiochip4`.
Check all pins against your own hardware and back up a working daemon before
installing it.

### BLE bridge installation

```bash
sudo apt install python3-dbus python3-gi bluez
sudo cp focuser_ble.py /home/stellarmate/focuser_ble.py
sudo chmod 755 /home/stellarmate/focuser_ble.py
sudo cp focuser-ble.service /etc/systemd/system/focuser-ble.service
sudo systemctl daemon-reload
sudo systemctl enable --now focuser-ble.service
```

Verify the service:

```bash
sudo systemctl status focuser-ble.service --no-pager -l
sudo journalctl -u focuser-ble.service -n 50 --no-pager
```

Successful startup prints:

```text
[BLE] GATT application registered
[BLE] TouchFocus-RPi ready
```

The device appears in TouchFocus as `TouchFocus-RPi`. BLE currently operates
without a PIN or bonding; use it only where that security model is acceptable.

## Mechanical configuration

When no configuration file exists, `focuserd` preserves the original working
calibration:

- 200 full steps per revolution
- 16 microsteps
- 1.313131 mm travel per motor revolution
- 42 mm maximum travel
- approximately 2436.923 steps/mm

SAVE on the Focuser Setup screen creates or updates:

```text
/var/lib/focuserd/focuser_config.json
```

The write is atomic. Invalid values, changes while jogging and a maximum below
the current physical position are rejected. Presets are stored separately and
are not deleted.

## Protocol

See [protocol.md](protocol.md). BLE and Wi-Fi carry the same JSON application
commands. Continuous jog is protected by a 350 ms daemon watchdog; if
heartbeats stop, the daemon stops the motor.

On the CONNECTION screen, **FIND** sends the existing `{"ping":1}` command to
the calculated broadcast address of the current Wi-Fi subnet. `focuserd`
already replies to the sender with `{"pong":1}`; TouchFocus takes the reply
source as the daemon address and stores it in NVS. Manual IP entry remains
available as a fallback.

## Power-management limitations

The backlight can turn off automatically and wake by touch or GPIO33. This is a
display/UI idle mode, not ESP32-P4 deep sleep. The verified BSP polls GT911 with
its interrupt pin set to `-1`, so hardware sleep would prevent touch wake-up.

The board documents an IP5306 charger but no verified battery-voltage ADC or
IP5306 telemetry connection to the P4. The status bar therefore displays
battery state as unavailable (`--`) instead of inventing a percentage. Real
measurement requires a verified IP5306 I2C connection or voltage-sense hardware.

### Wi-Fi OTA firmware update

The 16 MB partition table contains two 6.4 MB OTA application slots. Install
the first OTA-capable firmware over USB. For subsequent updates, connect
TouchFocus to Wi-Fi, open **SCREEN SETTINGS**, and press **ENABLE OTA**. The
screen shows the `.local` hostname and IP address. Motor control is stopped and
touch input is locked while an update is being written.

Copy `src/config/ota_private.h.example` to `src/config/ota_private.h`, replace
the placeholder with a private password, and build the first OTA-capable image
over USB. The private file is ignored by Git. In PlatformIO select the
`jc4880p4_ota` environment, supply the same password as the ESPOTA upload
authentication value, and upload to `TouchFocus.local`. OTA remains disabled
on the device when no private password of at least eight characters is built in.
Only install firmware built by this project for `jc4880p4` / `esp32p4_es`.
Do not use a normal ESP32-P4 image on the engineering-sample chip.

## Repository layout

```text
TouchFocus/
|-- boards/                 PlatformIO ESP32-P4 ES board definition
|-- lib/                    vendored BSP and touch support
|-- reference/              Raspberry Pi and legacy sources
|-- src/
|   |-- comm/               controller, UDP, BLE and transport selection
|   |-- config/             network and timing defaults
|   |-- lvgl_glue.c         verified display/touch integration
|   `-- main.cpp            UI and settings
|-- lv_conf.h               LVGL configuration
|-- platformio.ini          pinned build environment
`-- protocol.md             communication protocol
```

## Project status

TouchFocus is under active development. LCD, touch, PSRAM, LVGL, Wi-Fi/UDP,
BLE, presets and real motor control have been tested on the target hardware.
Treat the focuser as moving machinery: test new firmware unloaded and remain
ready to remove motor power.

## License

TouchFocus original code and documentation are licensed under the
[MIT License](LICENSE), copyright (c) 2026 mil-cha.

Vendored third-party components retain their own licenses and copyright
notices. In particular, `lib/guition-jc4880p4-bsp` is MIT-licensed by
ultramcu and `lib/esp_lcd_touch_gt911` is Apache-2.0-licensed by Espressif.
Files under `reference/` are retained as historical or deployment references;
their original notices and authorship apply where present.
