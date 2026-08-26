// SPDX-License-Identifier: MIT
#pragma once

#include <indifocuser.h>

#include <cstdint>
#include <string>

class TouchFocusFocuser final : public INDI::Focuser
{
public:
    TouchFocusFocuser();
    ~TouchFocusFocuser() override;

    const char *getDefaultName() override;
    bool initProperties() override;
    bool updateProperties() override;
    bool Connect() override;
    bool Disconnect() override;
    void TimerHit() override;
    bool ISNewNumber(const char *dev, const char *name, double values[],
                     char *names[], int n) override;
    bool ISNewSwitch(const char *dev, const char *name, ISState *states,
                     char *names[], int n) override;
    bool saveConfigItems(FILE *fp) override;

protected:
    IPState MoveAbsFocuser(uint32_t targetTicks) override;
    IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
    bool AbortFocuser() override;
    bool SyncFocuser(uint32_t ticks) override;

private:
    struct DaemonStatus
    {
        int32_t position = 0;
        bool moving = false;
        uint32_t maximum = 50000;
        bool temperatureValid = false;
        double temperature = 0;
        bool compensationEnabled = false;
        bool compensationActive = false;
        double coefficient = 0;
        double hysteresis = 0.3;
        double stepsPerMm = 0;
    };

    bool openSocket();
    void closeSocket();
    bool sendCommand(const std::string &command, std::string &reply);
    bool readStatus(DaemonStatus &status);
    bool readPresets();
    void updatePresetMillimeters();
    bool sendTemperatureSettings();
    void applyStatus(const DaemonStatus &status);

    int m_Socket = -1;
    std::string m_ServerAddress = "127.0.0.1";
    uint16_t m_ServerPort = 7625;
    uint64_t m_NextReconnectMs = 0;
    bool m_LastMoving = false;
    bool m_PropertiesReady = false;
    int32_t m_LastPosition = -1;
    double m_LastTemperature = -273.15;
    double m_StepsPerMm = 0;

    INDI::PropertyNumber TemperatureNP {1};
    INDI::PropertyNumber TouchFocusPresetsNP {9};
    INDI::PropertyNumber TouchFocusPresetMmNP {9};
    INDI::PropertySwitch TouchFocusPresetGotoSP {9};
    INDI::PropertySwitch TemperatureCompensationSP {2};
    INDI::PropertyNumber TemperatureSettingsNP {2};
    INDI::PropertyText DaemonInfoTP {2};
};
