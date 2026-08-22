// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

#include <Arduino.h>

namespace touchfocus {

struct FocuserStatus {
    bool connected = false;
    bool has_position = false;
    int32_t position_steps = 0;
    float position_mm = 0.0F;
    uint32_t last_message_ms = 0;
    bool has_config = false;
    bool config_ok = false;
    uint32_t config_revision = 0;
    int motor_steps = 200;
    int microsteps = 16;
    float travel_per_rev_mm = 1.313131F;
    float max_travel_mm = 42.0F;
    float steps_per_mm = 2436.923F;
};

class IFocuserTransport {
public:
    virtual ~IFocuserTransport() = default;
    virtual void begin() = 0;
    virtual void poll() = 0;
    virtual bool sendCommand(const char *json) = 0;
    virtual const FocuserStatus &status() const = 0;
};

}  // namespace touchfocus
