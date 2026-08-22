// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once
#include <BLEDevice.h>
#include "ifocuser_transport.h"

namespace touchfocus {
class BleFocuserTransport final : public IFocuserTransport {
public:
    void begin() override {}
    void poll() override;
    bool sendCommand(const char *json) override;
    const FocuserStatus &status() const override { return status_; }
    bool attach(BLEClient *client);
    bool isReady() const;
    const char *lastError() const { return last_error_; }
private:
    static void notify(BLERemoteCharacteristic *, uint8_t *, size_t, bool);
    void parse(const char *json);
    static BleFocuserTransport *active_;
    BLEClient *client_ = nullptr;
    BLERemoteCharacteristic *command_ = nullptr;
    BLERemoteCharacteristic *status_characteristic_ = nullptr;
    FocuserStatus status_;
    const char *last_error_ = "Not attached";
};
}
