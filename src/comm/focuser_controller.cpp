// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#include "focuser_controller.h"

#include "../config/network_config.h"

namespace touchfocus {

void FocuserController::setJogSpeed(int speed)
{
    jog_speed_ = constrain(speed, config::MIN_JOG_SPEED,
                           config::MAX_JOG_SPEED);
}

void FocuserController::begin()
{
    transport_.begin();
}

void FocuserController::poll()
{
    transport_.poll();
    const uint32_t now = millis();
    if ((motion_ == LocalMotion::In || motion_ == LocalMotion::Out) &&
        !fast_motion_ &&
        now - move_started_ms_ >= config::MOVE_ACCELERATE_AFTER_MS) {
        fast_motion_ = true;
        Serial.printf("[Focuser] Hold acceleration: continuous jog speed %d\n",
                      jog_speed_);
        sendMove();
    }
    if ((motion_ == LocalMotion::In || motion_ == LocalMotion::Out) &&
        now - last_move_ms_ >= (fast_motion_ ? config::JOG_HEARTBEAT_MS
                                             : config::MOVE_REPEAT_MS)) {
        sendMove();
    }
    if (motion_ == LocalMotion::Homing && now - homing_since_ms_ > 1000) {
        motion_ = LocalMotion::Idle;
    }
}

void FocuserController::sendMove()
{
    char json[48];
    if (fast_motion_) {
        const char *direction = motion_ == LocalMotion::In ? "IN" : "OUT";
        snprintf(json, sizeof(json), "{\"jog\":\"%s\",\"speed\":%d}",
                 direction, jog_speed_);
        transport_.sendCommand(json);
        last_move_ms_ = millis();
        return;
    }
    const char *key = motion_ == LocalMotion::In ? "move_in" : "move_out";
    snprintf(json, sizeof(json), "{\"%s\":%d}", key, move_steps_);
    transport_.sendCommand(json);
    last_move_ms_ = millis();
}

void FocuserController::moveIn(int steps)
{
    move_steps_ = steps;
    motion_ = LocalMotion::In;
    move_started_ms_ = millis();
    fast_motion_ = false;
    sendMove();
}

void FocuserController::moveOut(int steps)
{
    move_steps_ = steps;
    motion_ = LocalMotion::Out;
    move_started_ms_ = millis();
    fast_motion_ = false;
    sendMove();
}

void FocuserController::stop()
{
    motion_ = LocalMotion::Idle;
    fast_motion_ = false;
    // Repeat UDP STOP; the daemon heartbeat watchdog is the final failsafe.
    transport_.sendCommand("{\"jog\":\"STOP\"}");
    transport_.sendCommand("{\"jog\":\"STOP\"}");
    transport_.sendCommand("{\"jog\":\"STOP\"}");
    transport_.sendCommand("{\"joyx\":2000,\"sw\":0}");
}

void FocuserController::sendButton(uint8_t preset, bool long_press)
{
    if (preset < 1 || preset > 8) return;
    char json[64];
    snprintf(json, sizeof(json),
             long_press
                 ? "{\"joyx\":2000,\"sw\":0,\"b%u_long\":1}"
                 : "{\"joyx\":2000,\"sw\":0,\"b%u\":1}",
             preset);
    transport_.sendCommand(json);
}

bool FocuserController::goPreset(uint8_t preset)
{
    if (preset < 1 || preset > 9) return false;
    char json[24];
    snprintf(json, sizeof(json), "{\"preset\":%u}", preset);
    return transport_.sendCommand(json);
}

bool FocuserController::savePreset(uint8_t preset)
{
    if (preset < 1 || preset > 9) return false;
    char json[32];
    snprintf(json, sizeof(json), "{\"save_preset\":%u}", preset);
    return transport_.sendCommand(json);
}

bool FocuserController::requestConfig()
{
    return transport_.sendCommand("{\"get_config\":1}");
}

bool FocuserController::saveConfig(int motor_steps, int microsteps,
                                   float travel_per_rev_mm, float max_travel_mm)
{
    char json[192];
    snprintf(json, sizeof(json),
             "{\"set_config\":{\"motor_steps\":%d,\"microsteps\":%d,"
             "\"travel_per_rev_mm\":%.7f,\"max_travel_mm\":%.4f}}",
             motor_steps, microsteps, static_cast<double>(travel_per_rev_mm),
             static_cast<double>(max_travel_mm));
    return transport_.sendCommand(json);
}

void FocuserController::home()
{
    stop();
    sendButton(1, false);
    motion_ = LocalMotion::Homing;
    homing_since_ms_ = millis();
}

}  // namespace touchfocus
