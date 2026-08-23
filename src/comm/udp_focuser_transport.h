// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#pragma once

#include <WiFiUdp.h>

#include "ifocuser_transport.h"
#include "network_manager.h"
#include "../config/network_config.h"

namespace touchfocus {

class UdpFocuserTransport final : public IFocuserTransport {
public:
    explicit UdpFocuserTransport(NetworkManager &network) : network_(network) {}
    void begin() override;
    void poll() override;
    bool sendCommand(const char *json) override;
    const FocuserStatus &status() const override { return status_; }
    void setHost(const IPAddress &host);
    IPAddress host() const { return host_; }
    bool startDiscovery() override;
    bool discoveryRunning() const override { return discovery_running_; }
    bool discoveryFound() const override { return discovery_found_; }
    IPAddress discoveredHost() const override { return discovered_host_; }
    uint32_t discoveryRevision() const override { return discovery_revision_; }

private:
    void startSocketsIfNeeded();
    void stopSockets();
    void receiveStatus();
    void receiveCommandReply();
    void parseStatusJson(const char *json);

    WiFiUDP command_udp_;
    WiFiUDP status_udp_;
    NetworkManager &network_;
    IPAddress host_ = config::FOCUSER_HOST;
    FocuserStatus status_;
    bool sockets_started_ = false;
    uint32_t last_ping_ms_ = 0;
    bool sendDiscoveryPing();
    bool discovery_running_ = false;
    bool discovery_found_ = false;
    IPAddress discovered_host_;
    uint32_t discovery_started_ms_ = 0;
    uint32_t last_discovery_ping_ms_ = 0;
    uint32_t discovery_revision_ = 0;
};

}  // namespace touchfocus
