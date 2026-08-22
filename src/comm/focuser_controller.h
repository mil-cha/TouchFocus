// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

#include "ifocuser_transport.h"

namespace touchfocus {

enum class LocalMotion { Idle, In, Out, Homing };

class FocuserController {
public:
    explicit FocuserController(IFocuserTransport &transport) : transport_(transport) {}

    void begin();
    void poll();
    void moveIn(int steps = 2);
    void moveOut(int steps = 2);
    void stop();
    bool goPreset(uint8_t preset);
    bool savePreset(uint8_t preset);
    bool requestConfig();
    bool saveConfig(int motor_steps, int microsteps, float travel_per_rev_mm,
                    float max_travel_mm);
    void home();

    const FocuserStatus &status() const { return transport_.status(); }
    LocalMotion motion() const { return motion_; }
    bool isFastMotion() const { return fast_motion_; }

private:
    void sendMove();
    void sendButton(uint8_t preset, bool long_press);

    IFocuserTransport &transport_;
    LocalMotion motion_ = LocalMotion::Idle;
    int move_steps_ = 2;
    uint32_t last_move_ms_ = 0;
    uint32_t move_started_ms_ = 0;
    uint32_t homing_since_ms_ = 0;
    bool fast_motion_ = false;
};

}  // namespace touchfocus
