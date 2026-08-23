// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#include "udp_focuser_transport.h"

#include <cstdlib>
#include <cstring>

#include "../config/network_config.h"

namespace touchfocus {
namespace {

bool readJsonNumber(const char *json, const char *key, double &value)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *cursor = strstr(json, pattern);
    if (cursor == nullptr) return false;
    cursor = strchr(cursor + strlen(pattern), ':');
    if (cursor == nullptr) return false;
    char *end = nullptr;
    value = strtod(cursor + 1, &end);
    return end != cursor + 1;
}

}  // namespace

void UdpFocuserTransport::setHost(const IPAddress &host)
{
    host_ = host;
    status_.connected = false;
    status_.last_message_ms = 0;
    Serial.printf("[FocuserUDP] Daemon address set to %s:%u\n",
                  host_.toString().c_str(), config::FOCUSER_COMMAND_PORT);
}

bool UdpFocuserTransport::sendDiscoveryPing()
{
    if (!sockets_started_) return false;
    const uint32_t ip = static_cast<uint32_t>(network_.localIpAddress());
    const uint32_t mask = static_cast<uint32_t>(network_.subnetMask());
    const IPAddress broadcast((ip & mask) | ~mask);
    static constexpr char ping[] = "{\"ping\":1}";
    if (!command_udp_.beginPacket(broadcast, config::FOCUSER_COMMAND_PORT)) {
        return false;
    }
    command_udp_.write(reinterpret_cast<const uint8_t *>(ping), strlen(ping));
    const bool sent = command_udp_.endPacket() == 1;
    if (sent) {
        Serial.printf("[FocuserUDP] Discovery ping to %s:%u\n",
                      broadcast.toString().c_str(), config::FOCUSER_COMMAND_PORT);
    }
    return sent;
}

bool UdpFocuserTransport::startDiscovery()
{
    if (!network_.isConnected()) return false;
    startSocketsIfNeeded();
    if (!sockets_started_) return false;
    discovery_running_ = true;
    discovery_found_ = false;
    discovered_host_ = IPAddress();
    discovery_started_ms_ = millis();
    last_discovery_ping_ms_ = discovery_started_ms_;
    if (!sendDiscoveryPing()) {
        discovery_running_ = false;
        ++discovery_revision_;
        return false;
    }
    return true;
}

void UdpFocuserTransport::begin()
{
    startSocketsIfNeeded();
}

void UdpFocuserTransport::startSocketsIfNeeded()
{
    if (sockets_started_ || !network_.isConnected()) return;
    const bool command_ok = command_udp_.begin(config::LOCAL_COMMAND_PORT);
    const bool status_ok = status_udp_.begin(config::FOCUSER_STATUS_PORT);
    sockets_started_ = command_ok && status_ok;
    if (!sockets_started_) {
        command_udp_.stop();
        status_udp_.stop();
    } else {
        Serial.printf("[FocuserUDP] Listening on %u, daemon %s:%u\n",
                      config::FOCUSER_STATUS_PORT,
                      host_.toString().c_str(),
                      config::FOCUSER_COMMAND_PORT);
    }
}

void UdpFocuserTransport::stopSockets()
{
    if (!sockets_started_) return;
    command_udp_.stop();
    status_udp_.stop();
    sockets_started_ = false;
    status_.connected = false;
}

bool UdpFocuserTransport::sendCommand(const char *json)
{
    startSocketsIfNeeded();
    if (!sockets_started_ || json == nullptr) return false;
    if (!command_udp_.beginPacket(host_,
                                  config::FOCUSER_COMMAND_PORT)) return false;
    command_udp_.write(reinterpret_cast<const uint8_t *>(json), strlen(json));
    return command_udp_.endPacket() == 1;
}

void UdpFocuserTransport::parseStatusJson(const char *json)
{
    double position = 0;
    double position_mm = 0;
    const bool has_steps = readJsonNumber(json, "pos", position);
    const bool has_mm = readJsonNumber(json, "pos_mm", position_mm);
    double motor_steps = 0, microsteps = 0, travel = 0, max_travel = 0, steps_mm = 0;
    const bool has_config = readJsonNumber(json, "motor_steps", motor_steps) &&
                            readJsonNumber(json, "microsteps", microsteps) &&
                            readJsonNumber(json, "travel_per_rev_mm", travel) &&
                            readJsonNumber(json, "max_travel_mm", max_travel) &&
                            readJsonNumber(json, "steps_per_mm", steps_mm);
    if (!has_steps && !has_mm && !has_config) return;

    if (has_steps) status_.position_steps = static_cast<int32_t>(position);
    if (has_mm) status_.position_mm = static_cast<float>(position_mm);
    if (has_steps || has_mm) status_.has_position = true;
    if (has_config) {
        status_.motor_steps = static_cast<int>(motor_steps);
        status_.microsteps = static_cast<int>(microsteps);
        status_.travel_per_rev_mm = static_cast<float>(travel);
        status_.max_travel_mm = static_cast<float>(max_travel);
        status_.steps_per_mm = static_cast<float>(steps_mm);
        status_.has_config = true;
        ++status_.config_revision;
        status_.config_ok = strstr(json, "\"config_ok\": true") != nullptr ||
                            strstr(json, "\"config_ok\":true") != nullptr;
    }
    status_.last_message_ms = millis();
    status_.connected = true;
}

void UdpFocuserTransport::receiveStatus()
{
    int packet_size = 0;
    while ((packet_size = status_udp_.parsePacket()) > 0) {
        char buffer[192];
        const int length = status_udp_.read(buffer, sizeof(buffer) - 1);
        if (length <= 0) continue;
        buffer[length] = '\0';
        parseStatusJson(buffer);
    }
}

void UdpFocuserTransport::receiveCommandReply()
{
    while (command_udp_.parsePacket() > 0) {
        const IPAddress sender = command_udp_.remoteIP();
        char buffer[320];
        const int length = command_udp_.read(buffer, sizeof(buffer) - 1);
        if (length <= 0) continue;
        buffer[length] = '\0';
        if (strstr(buffer, "\"pong\"") != nullptr) {
            if (discovery_running_) {
                discovered_host_ = sender;
                discovery_found_ = true;
                discovery_running_ = false;
                setHost(sender);
                ++discovery_revision_;
                Serial.printf("[FocuserUDP] Discovered daemon at %s\n",
                              sender.toString().c_str());
            }
            status_.last_message_ms = millis();
            status_.connected = true;
        }
        parseStatusJson(buffer);
    }
}

void UdpFocuserTransport::poll()
{
    if (!network_.isConnected()) {
        stopSockets();
        status_.connected = false;
        return;
    }

    startSocketsIfNeeded();
    if (!sockets_started_) return;
    receiveStatus();
    receiveCommandReply();

    const uint32_t now = millis();
    if (discovery_running_) {
        if (now - discovery_started_ms_ >= 2500) {
            discovery_running_ = false;
            discovery_found_ = false;
            ++discovery_revision_;
            Serial.println("[FocuserUDP] Discovery timed out");
        } else if (now - last_discovery_ping_ms_ >= 350) {
            last_discovery_ping_ms_ = now;
            sendDiscoveryPing();
        }
    }
    if (now - last_ping_ms_ >= config::PING_INTERVAL_MS) {
        last_ping_ms_ = now;
        sendCommand("{\"ping\":1}");
    }
    status_.connected = status_.last_message_ms != 0 &&
                        now - status_.last_message_ms <= config::STATUS_TIMEOUT_MS;
}

}  // namespace touchfocus
