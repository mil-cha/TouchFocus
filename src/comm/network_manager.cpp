// SPDX-License-Identifier: MIT
// Copyright (c) 2026 mil-cha
#include "network_manager.h"

#include <WiFi.h>

namespace touchfocus {

void NetworkManager::begin(const String &saved_ssid, const String &saved_password)
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    if (!saved_ssid.isEmpty()) {
        WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
        Serial.printf("[Network] Auto-connecting to %s\n", saved_ssid.c_str());
    }
}

void NetworkManager::connect(const char *ssid, const char *password)
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
}

void NetworkManager::disconnect()
{
    WiFi.disconnect();
}

bool NetworkManager::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

String NetworkManager::ssid() const
{
    return WiFi.SSID();
}

String NetworkManager::localIp() const
{
    return WiFi.localIP().toString();
}

IPAddress NetworkManager::localIpAddress() const
{
    return WiFi.localIP();
}

IPAddress NetworkManager::subnetMask() const
{
    return WiFi.subnetMask();
}

int32_t NetworkManager::rssi() const
{
    return WiFi.RSSI();
}

int16_t NetworkManager::startScan(bool show_hidden)
{
    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    return WiFi.scanNetworks(true, show_hidden);
}

int16_t NetworkManager::scanComplete() const
{
    return WiFi.scanComplete();
}

String NetworkManager::scanSsid(int16_t index) const
{
    return WiFi.SSID(index);
}

void NetworkManager::clearScanResults()
{
    WiFi.scanDelete();
}

}  // namespace touchfocus
