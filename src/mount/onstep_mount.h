// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace touchfocus {

enum class MountDirection : uint8_t { North, South, East, West };

class OnStepMount {
public:
    void begin();
    void poll();
    void setHost(const IPAddress &host);
    bool isConnected();

    bool move(MountDirection direction);
    bool stop(MountDirection direction);
    bool stopAxes();
    bool emergencyStop();
    bool setRate(uint8_t rate);
    bool getAltAz(String &altitude, String &azimuth);
    bool getLocalTime(String &local_time);
    bool getLocation(double &latitude_deg, double &longitude_east_deg);
    bool setLocation(double latitude_deg, double longitude_east_deg);
    bool setDateTime(int year, int month, int day, int hour, int minute,
                     int second, int local_utc_offset_minutes);
    bool startTracking();
    bool setHomePosition();
    bool goHome();
    bool setParkPosition();
    bool park();
    bool backToLastTarget();
    bool toggleSpiralSearch();
    void setDirectionSwap(bool north_south, bool east_west);
    uint8_t rate() const { return rate_; }

private:
    bool connect();
    bool sendCommand(const char *command);
    bool queryCommand(const char *command, String &reply);
    bool booleanCommand(const char *command);
    bool zeroSuccessCommand(const char *command);
    static bool isAxis1(MountDirection direction);

    WiFiClient client_;
    IPAddress host_ {192, 168, 88, 60};
    uint16_t port_ = 9999;
    uint8_t rate_ = 4;
    uint32_t last_connect_attempt_ms_ = 0;
    uint32_t last_successful_contact_ms_ = 0;
    bool pending_axis1_stop_ = false;
    bool pending_axis2_stop_ = false;
    bool swap_north_south_ = false;
    bool swap_east_west_ = false;
};

}  // namespace touchfocus
