#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import gpiod
import time
import socket
import threading
import serial
import json
import os
import select
import signal
import sys

# === GPIO PINS (pozor: u gpiod jsou to line offsets; na RPi často sedí s BCM, ale ne vždy) ===
STEP_PIN = 17
DIR_PIN = 27
EN_PIN = 22
BUZZER_PIN = 15
PRESET_FILE = "/var/lib/focuserd/focuser_presets.json"
CONFIG_FILE = "/var/lib/focuserd/focuser_config.json"

EN_TIMEOUT = 2.0  # seconds before driver disables
JOG_TIMEOUT = 0.35  # stop continuous TouchFocus jog if UDP heartbeats disappear
LISTEN_PORT = 7625

# === Mechanical constants: steps to mm ===
MOTOR_STEPS = 200
MICROSTEPS = 16
# Exact effective legacy travel: preserves 0.55 * (36/26) * 200 * 16 steps/mm.
TRAVEL_PER_REV_MM = 1.0 / ((5.5 / 10.0) * (36 / 26))
STEPS_PER_MM = MOTOR_STEPS * MICROSTEPS / TRAVEL_PER_REV_MM
print("STEPS_PER_MM:", round(STEPS_PER_MM))  # Example: 2437

MIN_POS_MM = 0.0
MAX_POS_MM = 42.0
MIN_POS = int(round(MIN_POS_MM * STEPS_PER_MM))
MAX_POS = int(round(MAX_POS_MM * STEPS_PER_MM))

def config_dict():
    return {"motor_steps": MOTOR_STEPS, "microsteps": MICROSTEPS,
            "travel_per_rev_mm": TRAVEL_PER_REV_MM,
            "max_travel_mm": MAX_POS_MM, "steps_per_mm": STEPS_PER_MM}

def apply_config(values, persist=False):
    global MOTOR_STEPS, MICROSTEPS, TRAVEL_PER_REV_MM, MAX_POS_MM
    global STEPS_PER_MM, MAX_POS, position
    motor_steps = int(values["motor_steps"])
    microsteps = int(values["microsteps"])
    travel = float(values["travel_per_rev_mm"])
    max_travel = float(values["max_travel_mm"])
    if not (1 <= motor_steps <= 10000 and 1 <= microsteps <= 256 and
            0.0001 <= travel <= 1000.0 and 0.1 <= max_travel <= 1000.0):
        raise ValueError("configuration value outside allowed range")
    new_steps_per_mm = motor_steps * microsteps / travel
    old_position_mm = position / STEPS_PER_MM if STEPS_PER_MM > 0 else 0.0
    if old_position_mm > max_travel:
        raise ValueError("current position exceeds new maximum travel")
    MOTOR_STEPS, MICROSTEPS = motor_steps, microsteps
    TRAVEL_PER_REV_MM, MAX_POS_MM = travel, max_travel
    STEPS_PER_MM = new_steps_per_mm
    MAX_POS = int(round(MAX_POS_MM * STEPS_PER_MM))
    position = int(round(old_position_mm * STEPS_PER_MM))
    if persist:
        os.makedirs(os.path.dirname(CONFIG_FILE), exist_ok=True)
        temporary = CONFIG_FILE + ".tmp"
        with open(temporary, "w") as f:
            json.dump(config_dict(), f, indent=2)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temporary, CONFIG_FILE)

def load_config():
    if not os.path.exists(CONFIG_FILE):
        print("[CONFIG] Using unchanged legacy mechanical configuration")
        return
    try:
        with open(CONFIG_FILE, "r") as f:
            apply_config(json.load(f), persist=False)
        print("[CONFIG] Loaded:", config_dict())
    except Exception as e:
        print("[CONFIG] Invalid file; keeping legacy configuration:", e)

def steps_to_mm(steps):
    return steps / STEPS_PER_MM

def mm_to_steps(mm):
    return int(round(mm * STEPS_PER_MM))

# === State variables ===
position = 0
abort_flag = False
last_step_time = time.monotonic()
current_joy_dir = None  # None, 0 (IN), 1 (OUT)
current_joy_speed = 0
jog_deadline = 0.0
joy_stop_event = threading.Event()
load_config()

# === Presets ===
default_presets = [100, 200, 500, 1000, 2000, 5000, 10000, 20000, 30000]
presets = default_presets[:]

# === GPIO (libgpiod) wrapper ===
class GPIO:
    def __init__(self, chip_name="gpiochip4"):
        self.chip = gpiod.Chip(chip_name)
        self.lines = {}

    def claim_output(self, pin: int, initial: int = 0, consumer: str = "focuserd"):
        line = self.chip.get_line(pin)
        line.request(
            consumer=consumer,
            type=gpiod.LINE_REQ_DIR_OUT,
            default_vals=[1 if initial else 0],
        )
        self.lines[pin] = line

    def write(self, pin: int, value: int):
        self.lines[pin].set_value(1 if value else 0)

    def close(self):
        try:
            self.chip.close()
        except Exception:
            pass

gpio = GPIO("gpiochip4")

def setup_gpio():
    gpio.claim_output(STEP_PIN, initial=0, consumer="focuserd-step")
    gpio.claim_output(DIR_PIN,  initial=0, consumer="focuserd-dir")
    gpio.claim_output(EN_PIN,   initial=1, consumer="focuserd-en")    # driver disabled by default
    gpio.claim_output(BUZZER_PIN, initial=0, consumer="focuserd-buzz")

setup_gpio()

# === Preset load/save ===
def load_presets():
    global presets
    if os.path.exists(PRESET_FILE):
        try:
            with open(PRESET_FILE, "r") as f:
                presets = json.load(f)
            # Preserve older preset files and add missing TouchFocus P1-P9 slots.
            if not isinstance(presets, list):
                raise ValueError("preset file does not contain a list")
            presets = presets[:9]
            presets.extend(default_presets[len(presets):9])
            print("[PRESET] Loaded:", presets)
        except Exception as e:
            print("[PRESET] Cannot load presets, using defaults.", e)
            presets = default_presets[:]
    else:
        presets = default_presets[:]

def save_presets():
    try:
        with open(PRESET_FILE, "w") as f:
            json.dump(presets, f)
        print("[PRESET] Saved:", presets)
        print(f"[PRESET] Ukladam do: {os.path.abspath(PRESET_FILE)}")
    except Exception as e:
        print("[PRESET] Save error:", e)

load_presets()

# === Buzzer ===
def beep(duration=0.07):
    gpio.write(BUZZER_PIN, 1)
    time.sleep(duration)
    gpio.write(BUZZER_PIN, 0)

# === Driver disable watchdog ===
def en_watchdog_loop():
    global last_step_time
    while True:
        now = time.monotonic()
        if now - last_step_time > EN_TIMEOUT:
            gpio.write(EN_PIN, 1)  # disable driver
        time.sleep(0.2)

# === Stepper movement functions ===
def do_step(direction, delay):
    global position, last_step_time
    # Only allow direction 0 or 1, otherwise return
    if direction not in (0, 1):
        return
    if abort_flag:
        return
    if (direction == 0 and position <= MIN_POS) or (direction == 1 and position >= MAX_POS):
        return

    gpio.write(EN_PIN, 0)  # enable driver (LOW = enable)
    gpio.write(DIR_PIN, int(direction))

    gpio.write(STEP_PIN, 1)
    time.sleep(delay / 2)
    gpio.write(STEP_PIN, 0)
    time.sleep(delay / 2)

    # direction 0 = -1, direction 1 = +1
    position += 1 if direction else -1
    last_step_time = time.monotonic()

def move(direction_str, steps):
    direction = 0 if direction_str == "IN" else 1
    for _ in range(steps):
        if abort_flag:
            break
        if direction == 0 and position <= MIN_POS:
            break
        if direction == 1 and position >= MAX_POS:
            break
        do_step(direction, 0.0004)

def goto(target):
    global position, abort_flag

    try:
        target = int(target)
    except Exception:
        print("[GOTO] invalid target:", target)
        return

    # Enforce limits
    if target < MIN_POS:
        target = MIN_POS
    if target > MAX_POS:
        target = MAX_POS

    cur = int(position)
    if target == cur:
        return

    # Správně: když target > position, jedeme OUT (u tebe direction=1)
    direction = 1 if target > cur else 0

    steps = abs(target - cur)

    if steps <= 5:
        delay = 0.00035
    elif steps <= 50:
        delay = 0.00015
    else:
        delay = 0.0006

    for _ in range(steps):
        if abort_flag:
            break

        # limity podle směru
        if direction == 0 and position <= MIN_POS:   # IN
            break
        if direction == 1 and position >= MAX_POS:   # OUT
            break

        do_step(direction, delay)

def home():
    global position
    print("[HOME] Home set (reset position to 0).")
    beep(0.2)
    position = 0
    beep(0.5)
    print("[HOME] Done, home = 0 steps.")
    gpio.write(EN_PIN, 1)  # disable driver

def sync(pos):
    global position
    if pos < MIN_POS:
        position = MIN_POS
    elif pos > MAX_POS:
        position = MAX_POS
    else:
        position = pos

def abort():
    global abort_flag
    abort_flag = True
    time.sleep(0.1)
    abort_flag = False

# === Thread for joystick movement ===
def joystick_mover():
    global current_joy_dir, current_joy_speed, jog_deadline
    while True:
        # TouchFocus refreshes a continuous jog command while the button is held.
        # Stop safely if STOP or subsequent heartbeat packets are lost.
        if jog_deadline and time.monotonic() > jog_deadline:
            current_joy_dir = None
            current_joy_speed = 0
            jog_deadline = 0.0
            print("[JOG] Heartbeat timeout, STOP")

        if current_joy_dir in (0, 1) and current_joy_speed > 0:
            delay = max(0.00009, 0.002 / current_joy_speed)
            do_step(current_joy_dir, delay)
        else:
            time.sleep(0.01)

# === TCP socket server (for INDI/clients) ===
def handle_client(conn):
    addr = conn.getpeername()
    print(f"New connection from {addr}")
    global position, presets
    try:
        while True:
            data = conn.recv(128).decode().strip()
            if not data:
                break
            print(f"Received: {data}")
            parts = data.split()

            try:
                if data.startswith("GOTO"):
                    goto(int(parts[1]))
                    conn.sendall(b"OK\n")
                elif data.startswith("GOTOMM"):
                    goto(mm_to_steps(float(parts[1])))
                    conn.sendall(b"OK\n")
                elif data.startswith("MOVE"):
                    move(parts[1], int(parts[2]))
                    conn.sendall(b"OK\n")
                elif data == "HOME":
                    home()
                    conn.sendall(b"OK\n")
                elif data.startswith("SYNC"):
                    sync(int(parts[1]))
                    conn.sendall(b"OK\n")
                elif data == "ABORT":
                    abort()
                    conn.sendall(b"OK\n")
                elif data == "GETPOS":
                    conn.sendall(f"POS {position}\n".encode())
                elif data.startswith("GETPRESET"):
                    try:
                        n = int(parts[1]) - 1
                        if 0 <= n < len(presets):
                            conn.sendall(f"PRESET {n+1} {presets[n]}\n".encode())
                        else:
                            conn.sendall(b"ERR\n")
                    except Exception:
                        conn.sendall(b"ERR\n")
                elif data.startswith("SETPRESET"):
                    try:
                        n = int(parts[1]) - 1
                        val = int(parts[2])
                        if 0 <= n < len(presets):
                            presets[n] = val
                            save_presets()
                            conn.sendall(b"OK\n")
                        else:
                            conn.sendall(b"ERR\n")
                    except Exception:
                        conn.sendall(b"ERR\n")
                elif data == "LISTPRESETS":
                    conn.sendall(f"PRESETS {' '.join(map(str, presets[:6]))}\n".encode())
                elif data.startswith("STEP"):
                    try:
                        direction = int(parts[1])
                        steps = int(parts[2])
                        delay = float(parts[3]) if len(parts) > 3 else 0.003
                        for _ in range(steps):
                            do_step(direction, delay)
                        conn.sendall(b"OK\n")
                    except Exception as e:
                        print("STEP error:", e)
                        conn.sendall(b"ERR\n")
                else:
                    print(f"Unknown command: {data}")
                    conn.sendall(b"ERR\n")
            except Exception as e:
                print(f"Error processing command '{data}': {e}")
                conn.sendall(b"ERR\n")
    except Exception as e:
        print(f"Error handling client {addr}: {e}")
    finally:
        conn.close()
        print(f"Connection closed from {addr}")

def socket_server():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", LISTEN_PORT))
        s.listen()
        print("[INFO] focuserd listening on port", LISTEN_PORT)
        while True:
            conn, addr = s.accept()
            threading.Thread(target=handle_client, args=(conn,), daemon=True).start()

# === UDP Broadcast of current position ===
def udp_position_broadcast():
    global position
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    ok_logged = False
    last_err_log = 0.0
    last_ok_log = 0.0

    while True:
        try:
            pos_mm = steps_to_mm(position)
            msg = json.dumps({"pos": position, "pos_mm": pos_mm})
            udp_sock.sendto(msg.encode(), ("192.168.88.255", 40001))

            now = time.monotonic()

            # Jednou po navázání sítě řekni, že broadcast jede
            if not ok_logged:
                print("[UDP] Broadcast OK (network up)")
                ok_logged = True
                last_ok_log = now

            # Volitelný heartbeat 1× za 10 s (ať víš, že to běží, ale nespamuje to)
            if now - last_ok_log >= 10.0:
                print(f"[UDP] Broadcasting OK (pos={position}, pos_mm={pos_mm:.2f})")
                last_ok_log = now

            time.sleep(0.2)

        except OSError as e:
            # při chybě znovu umožni vypsat "Broadcast OK" až se síť vrátí
            ok_logged = False

            now = time.monotonic()
            # Loguj chybu max 1× za 15 s, ať to nezahlcuje journal
            if now - last_err_log >= 15.0:
                print(f"[UDP] Broadcast error: {e} (waiting for network...)")
                last_err_log = now

            time.sleep(2)

        except Exception as e:
            ok_logged = False
            print(f"[UDP] Unexpected broadcast error: {e}")
            time.sleep(2)



# === UDP control from Handcontrol (ESP) ===
def udp_control_loop():
    global current_joy_dir, current_joy_speed, jog_deadline, position

    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind(("0.0.0.0", 40000))
    print("[INFO] UDP Handcontrol listening on port 40000")

    def exp_steps(dx, base=1.7, scale=2.1, max_steps=120):
        exp = scale * ((abs(dx) - 200) / 800)
        steps = int(base ** exp)
        return min(max_steps, max(1, steps))

    # Rising edge tracking for buttons (press once)
    prev_buttons = {
        "b1": 0, "b2": 0, "b3": 0, "b4": 0,
        "b5": 0, "b6": 0, "b7": 0, "b8": 0,
        "b1_long": 0, "b2_long": 0, "b3_long": 0, "b4_long": 0,
        "b5_long": 0, "b6_long": 0, "b7_long": 0, "b8_long": 0,
        "sw": 0
    }

    preset_latch = False

    while True:
        ready, _, _ = select.select([udp_sock], [], [], 0.01)
        if not ready:
            continue

        data, addr = udp_sock.recvfrom(256)
        try:
            cmd = json.loads(data.decode())

            if "get_config" in cmd:
                response = {"config": config_dict(), "config_ok": True}
                udp_sock.sendto(json.dumps(response).encode(), addr)
                continue

            if "set_config" in cmd:
                response = {"config_ok": False}
                try:
                    if current_joy_dir in (0, 1):
                        raise ValueError("motor is moving")
                    apply_config(cmd["set_config"], persist=True)
                    response = {"config": config_dict(), "config_ok": True}
                    print("[CONFIG] Saved:", config_dict())
                except Exception as e:
                    response["config_error"] = str(e)
                    print("[CONFIG] Rejected:", e)
                udp_sock.sendto(json.dumps(response).encode(), addr)
                continue

            # Continuous jog from TouchFocus. The packet is repeated while IN/OUT
            # is held; the movement thread performs steps independently of UDP.
            if "jog" in cmd:
                jog = str(cmd.get("jog", "STOP")).upper()

                if jog == "STOP":
                    was_moving = current_joy_dir in (0, 1)
                    current_joy_dir = None
                    current_joy_speed = 0
                    jog_deadline = 0.0
                    if was_moving:
                        print("[JOG] STOP")
                    continue

                if jog in ("IN", "OUT"):
                    try:
                        speed = max(1, min(20, int(cmd.get("speed", 3))))
                    except (TypeError, ValueError):
                        speed = 3

                    direction = 0 if jog == "IN" else 1
                    changed = (current_joy_dir != direction or
                               current_joy_speed != speed or
                               jog_deadline == 0.0)
                    current_joy_dir = direction
                    current_joy_speed = speed
                    jog_deadline = time.monotonic() + JOG_TIMEOUT
                    if changed:
                        print(f"[JOG] {jog}, speed={speed}")
                    continue

                print(f"[JOG] Invalid command: {jog}")
                continue

            # Native TouchFocus commands keep P1 separate from legacy b1=HOME
            # and provide a ninth preset without affecting the old controller.
            if "save_preset" in cmd:
                slot = int(cmd.get("save_preset", 0))
                if 1 <= slot <= 9:
                    presets[slot - 1] = int(position)
                    save_presets()
                    print(f"[PRESET] Saved preset {slot}: {position}")
                else:
                    print(f"[PRESET] Invalid save slot: {slot}")
                continue

            if "preset" in cmd:
                slot = int(cmd.get("preset", 0))
                if 1 <= slot <= 9:
                    target = presets[slot - 1]
                    print(f"[PRESET] GOTO {slot} -> {target}")
                    goto(target)
                else:
                    print(f"[PRESET] Invalid GOTO slot: {slot}")
                continue

            # --- Read inputs ---
            joyx = int(cmd.get("joyx", 2048) or 2048)

            sw = int(cmd.get("sw", 0) or 0)
            b1 = int(cmd.get("b1", 0) or 0)
            b2 = int(cmd.get("b2", 0) or 0)
            b3 = int(cmd.get("b3", 0) or 0)
            b4 = int(cmd.get("b4", 0) or 0)
            b5 = int(cmd.get("b5", 0) or 0)
            b6 = int(cmd.get("b6", 0) or 0)
            b7 = int(cmd.get("b7", 0) or 0)
            b8 = int(cmd.get("b8", 0) or 0)

            b1_long = int(cmd.get("b1_long", 0) or 0)
            b2_long = int(cmd.get("b2_long", 0) or 0)
            b3_long = int(cmd.get("b3_long", 0) or 0)
            b4_long = int(cmd.get("b4_long", 0) or 0)
            b5_long = int(cmd.get("b5_long", 0) or 0)
            b6_long = int(cmd.get("b6_long", 0) or 0)
            b7_long = int(cmd.get("b7_long", 0) or 0)
            b8_long = int(cmd.get("b8_long", 0) or 0)

            # --- Uvolnění latch, když žádný preset není držen ---
            if (b2 == 0 and b3 == 0 and b4 == 0 and b5 == 0 and
                b6 == 0 and b7 == 0 and b8 == 0):
                preset_latch = False

            # --- Rising edge detection (pressed once) ---
            pressed = []
            current_map = {
                "b1": b1, "b2": b2, "b3": b3, "b4": b4, "b5": b5, "b6": b6, "b7": b7, "b8": b8,
                "b1_long": b1_long, "b2_long": b2_long, "b3_long": b3_long, "b4_long": b4_long,
                "b5_long": b5_long, "b6_long": b6_long, "b7_long": b7_long, "b8_long": b8_long,
                "sw": sw
            }

            for k, cur in current_map.items():
                if cur == 1 and prev_buttons[k] == 0:
                    pressed.append(k)
                prev_buttons[k] = cur

            # --- Handle button actions ONCE per press ---
            if "sw" in pressed:
                abort()

            if "b1" in pressed:
                home()

            # GOTO presets (short press) - jen jednou na držení tlačítka
            if not preset_latch:
                if "b2" in pressed:
                    preset_latch = True
                    print(f"[PRESET] GOTO 2 -> {presets[1]}")
                    goto(presets[1])
                if "b3" in pressed:
                    preset_latch = True
                    print(f"[PRESET] GOTO 3 -> {presets[2]}")
                    goto(presets[2])
                if "b4" in pressed:
                    preset_latch = True
                    print(f"[PRESET] GOTO 4 -> {presets[3]}")
                    goto(presets[3])
                if "b5" in pressed:
                    preset_latch = True
                    print(f"[PRESET] GOTO 5 -> {presets[4]}")
                    goto(presets[4])
                if "b6" in pressed:
                    preset_latch = True
                    print(f"[PRESET] GOTO 6 -> {presets[5]}")
                    goto(presets[5])
                if "b7" in pressed:
                    preset_latch = True
                    print(f"[PRESET] GOTO 7 -> {presets[6]}")
                    goto(presets[6])
                if "b8" in pressed:
                    preset_latch = True
                    print(f"[PRESET] GOTO 8 -> {presets[7]}")
                    goto(presets[7])

            # SAVE presets (long press) - tyhle klidně bez latch (je to “akce”)
            if "b1_long" in pressed:
                presets[0] = position
                save_presets()
                print("[PRESET] Saved preset 1:", position)

            if "b2_long" in pressed:
                presets[1] = position
                save_presets()
                print("[PRESET] Saved preset 2:", position)
            if "b3_long" in pressed:
                presets[2] = position
                save_presets()
                print("[PRESET] Saved preset 3:", position)
            if "b4_long" in pressed:
                presets[3] = position
                save_presets()
                print("[PRESET] Saved preset 4:", position)
            if "b5_long" in pressed:
                presets[4] = position
                save_presets()
                print("[PRESET] Saved preset 5:", position)
            if "b6_long" in pressed:
                presets[5] = position
                save_presets()
                print("[PRESET] Saved preset 6:", position)
            if "b7_long" in pressed:
                presets[6] = position
                save_presets()
                print("[PRESET] Saved preset 7:", position)
            if "b8_long" in pressed:
                presets[7] = position
                save_presets()
                print("[PRESET] Saved preset 8:", position)

            # --- Ping/Pong for connection test ---
            if "ping" in cmd:
                response = {"pong": 1}
                udp_sock.sendto(json.dumps(response).encode(), addr)
                continue

            # --- move_in/move_out commands from Handcontrol (fine jog) ---
            if "move_in" in cmd:
                steps = int(cmd["move_in"] or 0)
                if steps > 0:
                    move("IN", steps)
                continue

            if "move_out" in cmd:
                steps = int(cmd["move_out"] or 0)
                if steps > 0:
                    move("OUT", steps)
                continue

            # --- Joystick (analog) ---
            if joyx == 0:
                continue

            center = 2000
            deadzone_lo = center - 200
            deadzone_hi = center + 200
            dx = joyx - center

            if dx < 0 and joyx < deadzone_lo:
                current_joy_dir = 0
                current_joy_speed = exp_steps(dx)
            elif dx > 0 and joyx > deadzone_hi:
                current_joy_dir = 1
                current_joy_speed = exp_steps(dx)
            else:
                current_joy_dir = None
                current_joy_speed = 0

        except Exception as e:
            print("[UDP] Decode error:", e)


# --- graceful stop for systemd ---
def _sigterm_handler(signum, frame):
    print("[INFO] SIGTERM received, shutting down...")
    gpio.write(EN_PIN, 1)
    gpio.close()
    sys.exit(0)

signal.signal(signal.SIGTERM, _sigterm_handler)

# === Main ===
if __name__ == "__main__":
    try:
        threading.Thread(target=en_watchdog_loop, daemon=True).start()
        threading.Thread(target=udp_control_loop, daemon=True).start()
        threading.Thread(target=joystick_mover, daemon=True).start()
        threading.Thread(target=udp_position_broadcast, daemon=True).start()
        socket_server()
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        gpio.close()
