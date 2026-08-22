// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

#include <Arduino.h>

namespace touchfocus::config {

inline const IPAddress FOCUSER_HOST(192, 168, 88, 240);
constexpr uint16_t FOCUSER_COMMAND_PORT = 40000;
constexpr uint16_t FOCUSER_STATUS_PORT = 40001;
constexpr uint16_t LOCAL_COMMAND_PORT = 40002;
constexpr uint32_t STATUS_TIMEOUT_MS = 1500;
constexpr uint32_t PING_INTERVAL_MS = 1000;
constexpr uint32_t MOVE_REPEAT_MS = 40;
constexpr int DEFAULT_MOVE_STEPS = 2;
constexpr uint32_t MOVE_ACCELERATE_AFTER_MS = 2000;
// focuserd goto() uses a continuous ~0.6 ms step period for preset moves.
// speed=3 gives the daemon's jog loop ~0.667 ms step timing, close to presets.
constexpr uint32_t JOG_HEARTBEAT_MS = 100;
constexpr int JOG_SPEED = 3;

}  // namespace touchfocus::config
