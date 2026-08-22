// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#include "ble_focuser_transport.h"
#include <cstdlib>
#include <cstring>
#include "../config/network_config.h"

namespace touchfocus {
namespace {
const BLEUUID SERVICE("7a8b0001-6f32-4f1f-9d32-54f6a4a10001");
const BLEUUID COMMAND("7a8b0002-6f32-4f1f-9d32-54f6a4a10001");
const BLEUUID STATUS("7a8b0003-6f32-4f1f-9d32-54f6a4a10001");
bool number(const char *json, const char *key, double &out) {
    char pattern[32]; snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern); if (!p || !(p = strchr(p, ':'))) return false;
    char *end = nullptr; out = strtod(p + 1, &end); return end != p + 1;
}
}
BleFocuserTransport *BleFocuserTransport::active_ = nullptr;
bool BleFocuserTransport::attach(BLEClient *client) {
    client_ = client; command_ = nullptr; status_characteristic_ = nullptr;
    if (!client_ || !client_->isConnected()) { last_error_ = "BLE link lost"; return false; }
    BLERemoteService *service = client_->getService(SERVICE);
    if (!service) { last_error_ = "TouchFocus service not found"; return false; }
    command_ = service->getCharacteristic(COMMAND);
    status_characteristic_ = service->getCharacteristic(STATUS);
    if (!command_) { last_error_ = "Command characteristic missing"; return false; }
    if (!status_characteristic_) { last_error_ = "Status characteristic missing"; return false; }
    if (!status_characteristic_->canNotify()) { last_error_ = "Status notify unavailable"; return false; }
    active_ = this;
    status_characteristic_->registerForNotify(notify);
    client_->setMTU(185);
    last_error_ = "";
    return true;
}
bool BleFocuserTransport::isReady() const {
    return client_ && client_->isConnected() && command_ && status_characteristic_;
}
bool BleFocuserTransport::sendCommand(const char *json) {
    if (!isReady() || !json) return false;
    // The Raspberry Pi characteristic supports write-without-response.  Jog
    // commands are sent repeatedly while a button is held, so waiting for a
    // GATT acknowledgement here would unnecessarily throttle fine movement.
    command_->writeValue(reinterpret_cast<uint8_t *>(const_cast<char *>(json)), strlen(json), false);
    return true;
}
void BleFocuserTransport::notify(BLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
    if (!active_ || len >= 320) return;
    char json[320]; memcpy(json, data, len); json[len] = 0; active_->parse(json);
}
void BleFocuserTransport::parse(const char *json) {
    double v;
    if (number(json, "pos", v)) { status_.position_steps = static_cast<int32_t>(v); status_.has_position = true; }
    if (number(json, "pos_mm", v)) status_.position_mm = static_cast<float>(v);
    double ms, micro, travel, maximum, spm;
    if (number(json,"motor_steps",ms) && number(json,"microsteps",micro) &&
        number(json,"travel_per_rev_mm",travel) && number(json,"max_travel_mm",maximum) &&
        number(json,"steps_per_mm",spm)) {
        status_.motor_steps=ms; status_.microsteps=micro; status_.travel_per_rev_mm=travel;
        status_.max_travel_mm=maximum; status_.steps_per_mm=spm; status_.has_config=true;
        ++status_.config_revision;
    }
    status_.last_message_ms = millis(); status_.connected = true;
}
void BleFocuserTransport::poll() {
    status_.connected = isReady() && status_.last_message_ms &&
                        millis() - status_.last_message_ms <= config::STATUS_TIMEOUT_MS;
}
}
