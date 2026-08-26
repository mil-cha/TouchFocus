// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#include "onstep_mount.h"

namespace touchfocus {

void OnStepMount::begin()
{
    last_connect_attempt_ms_ = millis() - 2000;
}

void OnStepMount::setHost(const IPAddress &host)
{
    if (host_ == host) return;
    client_.stop();
    host_ = host;
    last_connect_attempt_ms_ = millis() - 2000;
}

bool OnStepMount::isConnected()
{
    return WiFi.status() == WL_CONNECTED &&
           (client_.connected() ||
            (last_successful_contact_ms_ != 0 &&
             millis() - last_successful_contact_ms_ < 3000));
}

bool OnStepMount::connect()
{
    if (WiFi.status() != WL_CONNECTED) return false;
    client_.stop();
    if (!client_.connect(host_, port_, 350)) return false;
    client_.setNoDelay(true);
    last_successful_contact_ms_ = millis();
    Serial.printf("[OnStep] Connected to %s:%u\n",
                  host_.toString().c_str(), port_);

    // Restore the selected manual rate after a TCP reconnect. If a release
    // happened while disconnected, deliver its axis-specific stop first.
    if (pending_axis1_stop_) {
        client_.print(":Qe#");
        pending_axis1_stop_ = false;
    }
    if (pending_axis2_stop_) {
        client_.print(":Qn#");
        pending_axis2_stop_ = false;
    }
    char rate_command[] = ":R0#";
    rate_command[2] = static_cast<char>('0' + rate_);
    client_.print(rate_command);
    return true;
}

void OnStepMount::poll()
{
    if (WiFi.status() != WL_CONNECTED) {
        client_.stop();
        return;
    }
    if (client_.connected()) return;
    const uint32_t now = millis();
    if (now - last_connect_attempt_ms_ < 2000) return;
    last_connect_attempt_ms_ = now;
    connect();
}

bool OnStepMount::sendCommand(const char *command)
{
    // UI connection state has a short anti-flicker grace period, but command
    // delivery must always validate the physical socket itself.
    if (!client_.connected() && !connect()) return false;
    const size_t length = strlen(command);
    if (client_.write(reinterpret_cast<const uint8_t *>(command), length) != length) {
        client_.stop();
        return false;
    }
    last_successful_contact_ms_ = millis();
    return true;
}

bool OnStepMount::queryCommand(const char *command, String &reply)
{
    reply = "";
    if (!client_.connected() && !connect()) return false;

    // Discard an incomplete late reply from a previous timed-out telemetry
    // request. The command channel is separate and remains deterministic.
    while (client_.available()) client_.read();
    const size_t length = strlen(command);
    if (client_.write(reinterpret_cast<const uint8_t *>(command), length) != length) {
        client_.stop();
        return false;
    }

    // OnStep is on the local LAN and normally replies within a few ms. Keep
    // this bounded; telemetry never shares the safety-critical motion socket.
    const uint32_t deadline = millis() + 350;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
        while (client_.available()) {
            const char value = static_cast<char>(client_.read());
            if (value == '#') {
                last_successful_contact_ms_ = millis();
                return !reply.isEmpty();
            }
            if (value != '\r' && value != '\n' && reply.length() < 31)
                reply += value;
        }
        delay(1);
    }
    client_.stop();
    return false;
}

bool OnStepMount::getAltAz(String &altitude, String &azimuth)
{
    return queryCommand(":GA#", altitude) && queryCommand(":GZ#", azimuth);
}

bool OnStepMount::getLocalTime(String &local_time)
{
    // Standard LX200/OnStep query: local time in HH:MM:SS format.
    return queryCommand(":GL#", local_time);
}

static bool parseDms(const String &value, double &degrees)
{
    if (value.length() < 4) return false;
    const int separator = value.indexOf('*');
    if (separator < 2) return false;
    const int minute_separator = value.indexOf(':', separator + 1);
    const char sign = value[0];
    if (sign != '+' && sign != '-') return false;
    const double whole_degrees = value.substring(1, separator).toDouble();
    const double minutes = minute_separator > separator
        ? value.substring(separator + 1, minute_separator).toDouble()
        : value.substring(separator + 1).toDouble();
    const double seconds = minute_separator > separator
        ? value.substring(minute_separator + 1).toDouble() : 0.0;
    degrees = whole_degrees + minutes / 60.0 + seconds / 3600.0;
    if (sign == '-') degrees = -degrees;
    return true;
}

bool OnStepMount::getLocation(double &latitude_deg, double &longitude_east_deg)
{
    String latitude;
    String longitude;
    double onstep_longitude = 0.0;
    if (!queryCommand(":GtH#", latitude) ||
        !queryCommand(":GgH#", longitude) ||
        !parseDms(latitude, latitude_deg) ||
        !parseDms(longitude, onstep_longitude)) return false;
    // OnStep/LX200 longitude is west-positive; the UI is east-positive.
    longitude_east_deg = -onstep_longitude;
    while (longitude_east_deg > 180.0) longitude_east_deg -= 360.0;
    while (longitude_east_deg < -180.0) longitude_east_deg += 360.0;
    return true;
}

bool OnStepMount::booleanCommand(const char *command)
{
    if (!client_.connected() && !connect()) return false;
    while (client_.available()) client_.read();
    if (client_.print(command) != strlen(command)) {
        client_.stop();
        return false;
    }
    const uint32_t deadline = millis() + 300;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
        while (client_.available()) {
            const char value = static_cast<char>(client_.read());
            if (value == '1') {
                last_successful_contact_ms_ = millis();
                return true;
            }
            if (value == '0') {
                last_successful_contact_ms_ = millis();
                return false;
            }
        }
        delay(1);
    }
    client_.stop();
    return false;
}

bool OnStepMount::zeroSuccessCommand(const char *command)
{
    if (!client_.connected() && !connect()) return false;
    while (client_.available()) client_.read();
    if (client_.print(command) != strlen(command)) {
        client_.stop();
        return false;
    }
    const uint32_t deadline = millis() + 350;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
        while (client_.available()) {
            const char value = static_cast<char>(client_.read());
            if (value >= '0' && value <= '9') {
                last_successful_contact_ms_ = millis();
                return value == '0';
            }
        }
        delay(1);
    }
    client_.stop();
    return false;
}

static void formatDms(double degrees, int degree_width, char *output,
                      size_t output_size)
{
    const char sign = degrees < 0.0 ? '-' : '+';
    double absolute = fabs(degrees);
    int whole_degrees = static_cast<int>(absolute);
    int total_seconds = static_cast<int>(lround(
        (absolute - whole_degrees) * 3600.0));
    if (total_seconds >= 3600) {
        ++whole_degrees;
        total_seconds -= 3600;
    }
    const int minutes = total_seconds / 60;
    const int seconds = total_seconds % 60;
    snprintf(output, output_size,
             degree_width == 2 ? "%c%02d*%02d:%02d" : "%c%03d*%02d:%02d",
             sign, whole_degrees, minutes, seconds);
}

bool OnStepMount::setLocation(double latitude_deg, double longitude_east_deg)
{
    if (latitude_deg < -90.0 || latitude_deg > 90.0 ||
        longitude_east_deg < -180.0 || longitude_east_deg > 180.0) return false;
    char latitude[16];
    char longitude[16];
    formatDms(latitude_deg, 2, latitude, sizeof(latitude));
    // LX200/OnStep longitude is west-positive; the UI uses conventional east-positive.
    formatDms(-longitude_east_deg, 3, longitude, sizeof(longitude));
    char command[24];
    snprintf(command, sizeof(command), ":St%s#", latitude);
    if (!booleanCommand(command)) return false;
    snprintf(command, sizeof(command), ":Sg%s#", longitude);
    return booleanCommand(command);
}

bool OnStepMount::setDateTime(int year, int month, int day, int hour,
                              int minute, int second,
                              int local_utc_offset_minutes)
{
    char command[24];
    snprintf(command, sizeof(command), ":SC%02d/%02d/%04d#",
             month, day, year);
    if (!booleanCommand(command)) return false;
    snprintf(command, sizeof(command), ":SL%02d:%02d:%02d#",
             hour, minute, second);
    if (!booleanCommand(command)) return false;

    // OnStep expects the offset added to local time to obtain UTC, opposite
    // to the usual local=UTC+offset notation.
    const int onstep_offset = -local_utc_offset_minutes;
    const char sign = onstep_offset < 0 ? '-' : '+';
    const int absolute = abs(onstep_offset);
    snprintf(command, sizeof(command), ":SG%c%02d:%02d#", sign,
             absolute / 60, absolute % 60);
    return booleanCommand(command);
}

bool OnStepMount::startTracking() { return booleanCommand(":Te#"); }
bool OnStepMount::setHomePosition() { return sendCommand(":hF#"); }
bool OnStepMount::goHome() { return sendCommand(":hC#"); }
bool OnStepMount::setParkPosition() { return booleanCommand(":hQ#"); }
bool OnStepMount::park() { return booleanCommand(":hP#"); }
bool OnStepMount::backToLastTarget() { return zeroSuccessCommand(":MS#"); }
bool OnStepMount::toggleSpiralSearch() { return sendCommand(":Mp#"); }

bool OnStepMount::isAxis1(MountDirection direction)
{
    return direction == MountDirection::East || direction == MountDirection::West;
}

bool OnStepMount::move(MountDirection direction)
{
    const char *command = nullptr;
    switch (direction) {
        case MountDirection::North:
            command = swap_north_south_ ? ":Ms#" : ":Mn#"; break;
        case MountDirection::South:
            command = swap_north_south_ ? ":Mn#" : ":Ms#"; break;
        case MountDirection::East:
            command = swap_east_west_ ? ":Mw#" : ":Me#"; break;
        case MountDirection::West:
            command = swap_east_west_ ? ":Me#" : ":Mw#"; break;
    }
    return sendCommand(command);
}

void OnStepMount::setDirectionSwap(bool north_south, bool east_west)
{
    swap_north_south_ = north_south;
    swap_east_west_ = east_west;
}

bool OnStepMount::stop(MountDirection direction)
{
    const bool axis1 = isAxis1(direction);
    const char *command = axis1 ? ":Qe#" : ":Qn#";
    const bool sent = sendCommand(command);
    if (!sent) {
        if (axis1) pending_axis1_stop_ = true;
        else pending_axis2_stop_ = true;
    }
    return sent;
}

bool OnStepMount::stopAxes()
{
    const bool axis1 = sendCommand(":Qe#");
    const bool axis2 = sendCommand(":Qn#");
    if (!axis1) pending_axis1_stop_ = true;
    if (!axis2) pending_axis2_stop_ = true;
    return axis1 && axis2;
}

bool OnStepMount::emergencyStop()
{
    const bool sent = sendCommand(":Q#");
    if (!sent) {
        pending_axis1_stop_ = true;
        pending_axis2_stop_ = true;
    }
    return sent;
}

bool OnStepMount::setRate(uint8_t rate)
{
    rate_ = constrain(rate, 3, 9);
    char command[] = ":R0#";
    command[2] = static_cast<char>('0' + rate_);
    return sendCommand(command);
}

}  // namespace touchfocus
