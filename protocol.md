# TouchFocus legacy focuserd protocol

This document records the legacy handcontroller protocol analyzed during
TouchFocus development and the behavior of `reference/focuserd.py`. It
describes current behavior, including known
limitations, rather than proposing a replacement protocol.

## Endpoints

| Direction | Transport | Endpoint | Purpose |
|---|---|---|---|
| Handcontroller to daemon | UDP/JSON | configurable IP, default `192.168.88.240:40000` | Commands and ping |
| Daemon to handcontroller | UDP broadcast/JSON | `192.168.88.255:40001` | Position every 200 ms |
| Local INDI/clients | TCP/text | `127.0.0.1:7625` | Separate protocol, not used by TouchFocus |

The original handcontroller listens on local UDP port 40001. There is no
authentication, sequence number, command ID or general acknowledgement.

## Local INDI TCP protocol

The INDI driver uses a persistent newline-delimited TCP connection to
`127.0.0.1:7625`. Existing text commands remain supported. GOTO and MOVE now
start a daemon worker and return `OK` immediately, allowing the same connection
to process `ABORT` while the motor is moving.

| Command | Reply / behavior |
|---|---|
| `GOTO steps` | `OK`, `BUSY`, or `ERR`; starts a non-blocking absolute move |
| `MOVE IN|OUT steps` | `OK`, `BUSY`, or `ERR`; starts a non-blocking relative move |
| `ABORT` | `OK`; stops jog/GOTO and disables the driver |
| `SYNC steps` | `OK`; changes the logical position without moving |
| `GETPOS` | `POS steps` |
| `GETSTATUS` | Position, moving flag, maximum, temperature, compensation state and steps/mm |
| `SETTEMPCOMP enabled coefficient hysteresis` | Atomically stores daemon-side compensation settings |

`GETSTATUS` returns:

```text
STATUS position moving maxPosition temperatureValid temperatureC compEnabled compActive coefficient hysteresis stepsPerMm
```

## Commands

All fields are JSON numbers. Extra `joyx` and `sw` fields are accepted.

| JSON | Daemon behavior |
|---|---|
| `{"move_in":N}` | Synchronously move `N` steps toward the minimum |
| `{"move_out":N}` | Synchronously move `N` steps toward the maximum |
| `{"joyx":X,"sw":0}` | Set continuous joystick direction/speed; `X=2000` is neutral |
| `{"sw":1}` | Call `abort()` |
| `{"b1":1}` | Call `home()`; this resets the position counter to zero without seeking an endstop |
| `{"b2":1}` | Call `goto(2000)` |
| `{"b3":1}` ... `{"b8":1}` | Call `goto(3000)` |
| `{"bN_long":1}` | Save current position in preset slot N |
| `{"preset":N}` | TouchFocus: move to preset P1-P9 |
| `{"save_preset":N}` | TouchFocus: save current position to P1-P9 |
| `{"ping":1}` | Reply to sender with `{"pong":1}` |
| `{"stop":1}` | Immediately stop jog and interrupt an active GOTO/preset move |
| `{"get_temp_config":1}` | Return the saved temperature-compensation settings |
| `{"set_temp_config":{"temp_comp_enabled":true,"temp_coefficient":-120.0,"temp_hysteresis":0.3}}` | Atomically save temperature-compensation settings |

TouchFocus can also send the same ping to the broadcast address calculated
from its DHCP address and subnet mask. The source address of the first valid
pong becomes the saved daemon address. This discovery adds no new command and
does not change compatibility with existing clients.

The handcontroller maps joystick magnitude 1..6 to finite command sizes
2, 5, 10, 20, 50 and 100 steps and intends to repeat them every 40 ms. Its
150 ms display delay limits the effective loop rate. TouchFocus retains the
40 ms repeat interval and initially uses the safest 2-step size.
After an IN or OUT control remains held continuously for 2000 ms, TouchFocus
switches to the watchdog-protected continuous jog command. Releasing the
control sends STOP three times and resets the next press to finite 2-step moves.

The updated daemon has an explicit STOP command. For compatibility with older
daemon versions, TouchFocus also sends jog STOP and the existing neutral
joystick packet `{"joyx":2000,"sw":0}`.

## TouchFocus safe continuous-jog extension

To avoid motor speed modulation from finite UDP batches, the updated daemon
also accepts:

| JSON | Behavior |
|---|---|
| `{"jog":"IN","speed":3}` | Continuous IN movement and heartbeat refresh |
| `{"jog":"OUT","speed":3}` | Continuous OUT movement and heartbeat refresh |
| `{"jog":"STOP"}` | Immediate jog stop |

TouchFocus repeats an active jog heartbeat every 100 ms. The daemon stops jog
movement automatically if no heartbeat arrives for 350 ms, so Wi-Fi loss or a
lost touch-release packet cannot leave the motor running. Speed 3 produces a
step period near 0.667 ms, close to the daemon's preset `goto()` speed.

Preset GOTO runs in a daemon worker thread so the UDP receive loop remains able
to process `{"stop":1}` immediately. TouchFocus sends the dedicated STOP three
times as protection against a lost UDP packet. A stopped move retains the
current position counter.

## Status

Every 200 ms the daemon broadcasts:

```json
{"pos":1234,"pos_mm":0.5064,"temperature_valid":true,"temp_comp_enabled":true,"temp_comp_active":true,"temperature_c":8.625}
```

`temperature_c` is omitted when no valid DS18B20 reading is available.
`temperature_valid` therefore clears a previously displayed measurement after
a sensor fault. `temp_comp_active` means that compensation has a valid
temperature/position reference; it does not mean that the motor is currently
moving. No general moving, idle, homing, error or command-result field is present. TouchFocus
considers the daemon connected when a valid position broadcast or pong arrived
within 1500 ms.

TouchFocus sends `save_preset` once after a preset button is held for 900 ms.
The subsequent click is suppressed, so saving cannot immediately start a move.
The daemon preserves existing preset values and extends older seven- or
eight-slot files with defaults up to P9.

## Original input timing

- Long press threshold: more than 900 ms.
- Short press: more than 30 ms and less than 900 ms.
- A preset command is sent once on button release.
- Movement commands are repeated while the joystick remains displaced.
- No command retry or acknowledgement exists beyond repetition of movement.
- Wi-Fi connection in the legacy controller blocks until one of three embedded
  credentials connects; TouchFocus instead uses its Settings/NVS configuration.

## Known daemon behavior and mismatches

- `goto()` deliberately retains a documented reversed direction calculation.
- Short preset commands do not read the saved `presets` array: B2 is hardcoded
  to 2000 and B3..B8 to 3000.
- B1 short press is HOME, while B1 long press saves preset slot 1; that saved
  value is not used by B1 short press.
- The default preset list has seven entries, but B8 long press writes index 7
  and can raise `IndexError`.
- The protocol has no B9/P9 field.
- HOME is a position reset, not a physical homing/endstop movement.

TouchFocus does not correct these daemon behaviors in its first compatible
implementation.

## BLE transport extension

The optional Raspberry Pi bridge advertises `TouchFocus-RPi` and forwards the
same JSON application messages to focuserd UDP port 40000. It never accesses
GPIO. Service UUID `7a8b0001-6f32-4f1f-9d32-54f6a4a10001` contains command
write UUID `...0002...` and status-notify UUID `...0003...`. Position/status
notifications use the same JSON objects as UDP. Continuous jog retains the
daemon's 350 ms heartbeat watchdog.
