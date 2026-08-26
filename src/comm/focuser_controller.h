// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

#include "ifocuser_transport.h"
#include "../config/network_config.h"

namespace touchfocus {

enum class LocalMotion { Idle, In, Out, Homing };

class FocuserController {
public:
    explicit FocuserController(IFocuserTransport &transport) : transport_(transport) {}

    void begin();
    void poll();
    void moveIn(int steps = 4);
    void moveOut(int steps = 4);
    void stop();
    bool goPreset(uint8_t preset);
    bool savePreset(uint8_t preset);
    bool requestConfig();
    bool saveConfig(int motor_steps, int microsteps, float travel_per_rev_mm,
                    float max_travel_mm);
    bool requestTemperatureConfig();
    bool saveTemperatureConfig(bool enabled, float coefficient_steps_per_c,
                               float hysteresis_c);
    void home();
    bool findFocuser() { return transport_.startDiscovery(); }
    bool discoveryRunning() const { return transport_.discoveryRunning(); }
    bool discoveryFound() const { return transport_.discoveryFound(); }
    IPAddress discoveredHost() const { return transport_.discoveredHost(); }
    uint32_t discoveryRevision() const { return transport_.discoveryRevision(); }
    void setJogSpeed(int speed);
    int jogSpeed() const { return jog_speed_; }

    const FocuserStatus &status() const { return transport_.status(); }
    LocalMotion motion() const { return motion_; }
    bool isFastMotion() const { return fast_motion_; }

private:
    void sendMove();
    void sendButton(uint8_t preset, bool long_press);

    IFocuserTransport &transport_;
    LocalMotion motion_ = LocalMotion::Idle;
    int move_steps_ = 4;
    uint32_t last_move_ms_ = 0;
    uint32_t move_started_ms_ = 0;
    uint32_t homing_since_ms_ = 0;
    bool fast_motion_ = false;
    int jog_speed_ = config::DEFAULT_JOG_SPEED;
};

}  // namespace touchfocus
