// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

#include <Arduino.h>

namespace touchfocus {

class NetworkManager {
public:
    static constexpr int16_t SCAN_RUNNING = -1;
    static constexpr int16_t SCAN_FAILED = -2;
    void begin(const String &saved_ssid, const String &saved_password);
    void connect(const char *ssid, const char *password);
    void disconnect();

    bool isConnected() const;
    String ssid() const;
    String localIp() const;
    int32_t rssi() const;

    int16_t startScan(bool show_hidden = true);
    int16_t scanComplete() const;
    String scanSsid(int16_t index) const;
    void clearScanResults();
};

}  // namespace touchfocus
