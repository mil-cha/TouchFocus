// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once
#include "ifocuser_transport.h"
#include "ble_focuser_transport.h"
namespace touchfocus {
class AutoFocuserTransport final : public IFocuserTransport {
public:
 AutoFocuserTransport(IFocuserTransport &wifi, BleFocuserTransport &ble):wifi_(wifi),ble_(ble){}
 void begin() override { wifi_.begin(); ble_.begin(); }
 void poll() override { wifi_.poll(); ble_.poll(); }
 bool sendCommand(const char *j) override { return ble_.isReady() ? ble_.sendCommand(j) : wifi_.sendCommand(j); }
 const FocuserStatus &status() const override { return ble_.status().connected ? ble_.status() : wifi_.status(); }
private: IFocuserTransport &wifi_; BleFocuserTransport &ble_;
}; }
