// SPDX-License-Identifier: MIT
#include "indi_touchfocus_focuser.h"

#include <indicom.h>
#include <indidevapi.h>

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
constexpr const char *SETTINGS_TAB = "Temperature Compensation";
constexpr const char *PRESETS_TAB = "Presets";
constexpr int SOCKET_TIMEOUT_SECONDS = 2;
std::unique_ptr<TouchFocusFocuser> g_Focuser(new TouchFocusFocuser());
}

TouchFocusFocuser::TouchFocusFocuser()
{
    setVersion(0, 3);
    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE |
                      FOCUSER_CAN_ABORT | FOCUSER_CAN_SYNC);
    setSupportedConnections(CONNECTION_NONE);
}

TouchFocusFocuser::~TouchFocusFocuser()
{
    closeSocket();
}

const char *TouchFocusFocuser::getDefaultName()
{
    return "Focuserd";
}

bool TouchFocusFocuser::initProperties()
{
    INDI::Focuser::initProperties();

    FocusAbsPosNP[0].setMin(0);
    FocusAbsPosNP[0].setMax(50000);
    FocusAbsPosNP[0].setStep(10);
    FocusRelPosNP[0].setMin(0);
    FocusRelPosNP[0].setMax(50000);
    FocusRelPosNP[0].setStep(10);
    FocusMaxPosNP.setPermission(IP_RO);

    for (int index = 0; index < 9; ++index)
    {
        const std::string numberName = "PRESET_" + std::to_string(index + 1);
        const std::string label = "Preset " + std::to_string(index + 1);
        TouchFocusPresetsNP[index].fill(numberName.c_str(), label.c_str(), "%.f",
                                        0, 50000, 10, 0);
        TouchFocusPresetMmNP[index].fill(numberName.c_str(), label.c_str(), "%.3f",
                                         0, 1000, 0, 0);
        TouchFocusPresetGotoSP[index].fill(numberName.c_str(), label.c_str(), ISS_OFF);
    }
    TouchFocusPresetsNP.fill(getDeviceName(), "TOUCHFOCUS_PRESETS", "Presets P1-P9",
                             PRESETS_TAB, IP_RW, 0, IPS_IDLE);
    TouchFocusPresetMmNP.fill(getDeviceName(), "TOUCHFOCUS_PRESETS_MM",
                              "Preset positions (mm)", PRESETS_TAB,
                              IP_RO, 0, IPS_IDLE);
    TouchFocusPresetGotoSP.fill(getDeviceName(), "TOUCHFOCUS_GOTO", "Goto preset",
                                PRESETS_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    TemperatureNP[0].fill("TEMPERATURE", "Celsius", "%.2f", -55, 125, 0, 0);
    TemperatureNP.fill(getDeviceName(), "FOCUS_TEMPERATURE", "Temperature",
                       MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    TemperatureCompensationSP[0].fill("ENABLED", "Enabled", ISS_OFF);
    TemperatureCompensationSP[1].fill("DISABLED", "Disabled", ISS_ON);
    TemperatureCompensationSP.fill(getDeviceName(), "FOCUS_TEMPERATURE_COMPENSATION",
                                   "Compensation", SETTINGS_TAB, IP_RW,
                                   ISR_1OFMANY, 0, IPS_IDLE);

    TemperatureSettingsNP[0].fill("COEFFICIENT", "Steps / C", "%.3f",
                                  -100000, 100000, 1, 0);
    TemperatureSettingsNP[1].fill("HYSTERESIS", "Hysteresis C", "%.2f",
                                  0.05, 10, 0.05, 0.3);
    TemperatureSettingsNP.fill(getDeviceName(), "FOCUS_TEMPERATURE_SETTINGS",
                               "Parameters", SETTINGS_TAB, IP_RW, 0, IPS_IDLE);

    DaemonInfoTP[0].fill("ADDRESS", "Address", "127.0.0.1");
    DaemonInfoTP[1].fill("PORT", "Port", "7625");
    DaemonInfoTP.fill(getDeviceName(), "FOCUSER_DAEMON", "Daemon",
                      INFO_TAB, IP_RO, 0, IPS_IDLE);

    setDefaultPollingPeriod(500);
    addDebugControl();
    return true;
}

bool TouchFocusFocuser::updateProperties()
{
    INDI::Focuser::updateProperties();
    if (isConnected())
    {
        // INDI::Focuser supplies only three standard presets. Replace those
        // properties with the nine slots implemented by TouchFocus/focuserd.
        deleteProperty(PresetNP);
        deleteProperty(PresetGotoSP);
        defineProperty(TouchFocusPresetsNP);
        defineProperty(TouchFocusPresetMmNP);
        defineProperty(TouchFocusPresetGotoSP);
        defineProperty(TemperatureNP);
        defineProperty(TemperatureCompensationSP);
        defineProperty(TemperatureSettingsNP);
        defineProperty(DaemonInfoTP);
        m_PropertiesReady = true;
        SetTimer(getCurrentPollingPeriod());
    }
    else
    {
        m_PropertiesReady = false;
        deleteProperty(TouchFocusPresetsNP);
        deleteProperty(TouchFocusPresetMmNP);
        deleteProperty(TouchFocusPresetGotoSP);
        deleteProperty(TemperatureNP);
        deleteProperty(TemperatureCompensationSP);
        deleteProperty(TemperatureSettingsNP);
        deleteProperty(DaemonInfoTP);
    }
    return true;
}

bool TouchFocusFocuser::openSocket()
{
    closeSocket();
    m_Socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_Socket < 0)
        return false;

    timeval timeout {SOCKET_TIMEOUT_SECONDS, 0};
    setsockopt(m_Socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(m_Socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(m_ServerPort);
    if (inet_pton(AF_INET, m_ServerAddress.c_str(), &address.sin_addr) != 1 ||
        connect(m_Socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        closeSocket();
        return false;
    }
    return true;
}

void TouchFocusFocuser::closeSocket()
{
    if (m_Socket >= 0)
        close(m_Socket);
    m_Socket = -1;
}

bool TouchFocusFocuser::Connect()
{
    if (!openSocket())
    {
        LOGF_ERROR("Cannot connect to focuserd at %s:%u: %s",
                   m_ServerAddress.c_str(), m_ServerPort, strerror(errno));
        return false;
    }

    DaemonStatus status;
    if (!readStatus(status))
    {
        LOG_ERROR("Connected socket, but focuserd did not return a valid status.");
        closeSocket();
        return false;
    }
    applyStatus(status);
    if (!readPresets())
        LOG_WARN("Connected, but focuserd presets could not be read.");
    LOGF_INFO("Connected to focuserd at %s:%u.",
              m_ServerAddress.c_str(), m_ServerPort);
    return true;
}

bool TouchFocusFocuser::Disconnect()
{
    closeSocket();
    return true;
}

bool TouchFocusFocuser::sendCommand(const std::string &command, std::string &reply)
{
    if (m_Socket < 0)
        return false;

    const std::string request = command + "\n";
    size_t sent = 0;
    while (sent < request.size())
    {
        const ssize_t count = send(m_Socket, request.data() + sent,
                                   request.size() - sent, MSG_NOSIGNAL);
        if (count <= 0)
        {
            closeSocket();
            return false;
        }
        sent += static_cast<size_t>(count);
    }

    reply.clear();
    while (reply.size() < 512)
    {
        char value = 0;
        const ssize_t count = recv(m_Socket, &value, 1, 0);
        if (count <= 0)
        {
            closeSocket();
            return false;
        }
        if (value == '\n')
            return true;
        if (value != '\r')
            reply.push_back(value);
    }
    closeSocket();
    return false;
}

bool TouchFocusFocuser::readStatus(DaemonStatus &status)
{
    std::string reply;
    if (!sendCommand("GETSTATUS", reply))
        return false;

    int moving = 0, temperatureValid = 0, compensationEnabled = 0;
    int compensationActive = 0;
    const int matched = sscanf(reply.c_str(),
        "STATUS %d %d %u %d %lf %d %d %lf %lf %lf",
        &status.position, &moving, &status.maximum, &temperatureValid,
        &status.temperature, &compensationEnabled, &compensationActive,
        &status.coefficient, &status.hysteresis, &status.stepsPerMm);
    if (matched < 9)
    {
        LOGF_ERROR("Invalid focuserd status: %s", reply.c_str());
        return false;
    }
    status.moving = moving != 0;
    status.temperatureValid = temperatureValid != 0;
    status.compensationEnabled = compensationEnabled != 0;
    status.compensationActive = compensationActive != 0;
    return true;
}

bool TouchFocusFocuser::readPresets()
{
    std::string reply;
    if (!sendCommand("LISTPRESETS", reply))
        return false;

    int values[9] {};
    const int matched = sscanf(reply.c_str(),
        "PRESETS %d %d %d %d %d %d %d %d %d",
        &values[0], &values[1], &values[2], &values[3], &values[4],
        &values[5], &values[6], &values[7], &values[8]);
    if (matched != 9)
    {
        LOGF_ERROR("Invalid focuserd preset list: %s", reply.c_str());
        return false;
    }

    for (int index = 0; index < 9; ++index)
        TouchFocusPresetsNP[index].setValue(values[index]);
    TouchFocusPresetsNP.setState(IPS_OK);
    updatePresetMillimeters();
    if (m_PropertiesReady)
        TouchFocusPresetsNP.apply();
    return true;
}

void TouchFocusFocuser::updatePresetMillimeters()
{
    if (m_StepsPerMm <= 0)
    {
        TouchFocusPresetMmNP.setState(IPS_ALERT);
    }
    else
    {
        for (int index = 0; index < 9; ++index)
            TouchFocusPresetMmNP[index].setValue(
                TouchFocusPresetsNP[index].getValue() / m_StepsPerMm);
        TouchFocusPresetMmNP.setState(IPS_OK);
    }
    if (m_PropertiesReady)
        TouchFocusPresetMmNP.apply();
}

void TouchFocusFocuser::applyStatus(const DaemonStatus &status)
{
    if (status.stepsPerMm > 0 &&
        std::fabs(m_StepsPerMm - status.stepsPerMm) > 0.000001)
    {
        m_StepsPerMm = status.stepsPerMm;
        const double maximumMm = status.maximum / m_StepsPerMm;
        for (int index = 0; index < 9; ++index)
            TouchFocusPresetMmNP[index].setMax(maximumMm);
        TouchFocusPresetMmNP.updateMinMax();
        updatePresetMillimeters();
    }

    if (FocusAbsPosNP[0].getMax() != status.maximum)
    {
        FocusAbsPosNP[0].setMax(status.maximum);
        FocusRelPosNP[0].setMax(status.maximum);
        FocusSyncNP[0].setMax(status.maximum);
        for (int index = 0; index < 9; ++index)
            TouchFocusPresetsNP[index].setMax(status.maximum);
        FocusMaxPosNP[0].setValue(status.maximum);
        FocusMaxPosNP[0].setMax(status.maximum);
        FocusAbsPosNP.updateMinMax();
        FocusRelPosNP.updateMinMax();
        FocusSyncNP.updateMinMax();
        TouchFocusPresetsNP.updateMinMax();
        FocusMaxPosNP.updateMinMax();
        if (m_PropertiesReady)
        {
            FocusMaxPosNP.apply();
            FocusSyncNP.apply();
            TouchFocusPresetsNP.apply();
        }
    }

    if (m_LastPosition != status.position)
    {
        FocusAbsPosNP[0].setValue(status.position);
        if (m_PropertiesReady)
            FocusAbsPosNP.apply();
        m_LastPosition = status.position;
    }

    if (status.temperatureValid)
    {
        TemperatureNP[0].setValue(status.temperature);
        TemperatureNP.setState(IPS_OK);
        if (std::fabs(m_LastTemperature - status.temperature) >= 0.05)
        {
            if (m_PropertiesReady)
                TemperatureNP.apply();
            m_LastTemperature = status.temperature;
        }
    }
    else if (TemperatureNP.getState() != IPS_ALERT)
    {
        TemperatureNP.setState(IPS_ALERT);
        if (m_PropertiesReady)
            TemperatureNP.apply("DS18B20 is not available.");
    }

    const IPState compensationState = status.compensationEnabled &&
                                      !status.compensationActive ? IPS_BUSY : IPS_OK;
    const bool compensationChanged =
        (TemperatureCompensationSP[0].getState() == ISS_ON) != status.compensationEnabled ||
        TemperatureCompensationSP.getState() != compensationState ||
        std::fabs(TemperatureSettingsNP[0].getValue() - status.coefficient) > 0.0005 ||
        std::fabs(TemperatureSettingsNP[1].getValue() - status.hysteresis) > 0.0005;
    TemperatureCompensationSP.reset();
    TemperatureCompensationSP[status.compensationEnabled ? 0 : 1].setState(ISS_ON);
    TemperatureCompensationSP.setState(compensationState);
    TemperatureSettingsNP[0].setValue(status.coefficient);
    TemperatureSettingsNP[1].setValue(status.hysteresis);
    TemperatureSettingsNP.setState(IPS_OK);
    if (compensationChanged && m_PropertiesReady)
    {
        TemperatureCompensationSP.apply();
        TemperatureSettingsNP.apply();
    }

    if (status.moving)
    {
        if (FocusAbsPosNP.getState() != IPS_BUSY)
        {
            FocusAbsPosNP.setState(IPS_BUSY);
            if (m_PropertiesReady)
                FocusAbsPosNP.apply();
        }
    }
    else if (m_LastMoving || FocusAbsPosNP.getState() == IPS_BUSY ||
             FocusRelPosNP.getState() == IPS_BUSY ||
             TouchFocusPresetGotoSP.getState() == IPS_BUSY)
    {
        FocusAbsPosNP.setState(IPS_OK);
        FocusRelPosNP.setState(IPS_OK);
        TouchFocusPresetGotoSP.setState(IPS_OK);
        TouchFocusPresetGotoSP.reset();
        if (m_PropertiesReady)
        {
            FocusAbsPosNP.apply();
            FocusRelPosNP.apply();
            TouchFocusPresetGotoSP.apply();
        }
        LOG_INFO("Focuser movement completed.");
    }
    m_LastMoving = status.moving;
}

void TouchFocusFocuser::TimerHit()
{
    if (!isConnected())
        return;

    DaemonStatus status;
    if (m_Socket >= 0 && readStatus(status))
    {
        applyStatus(status);
    }
    else
    {
        const uint64_t now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        if (now >= m_NextReconnectMs)
        {
            m_NextReconnectMs = now + 2000;
            if (openSocket() && readStatus(status))
            {
                LOG_INFO("Reconnected to focuserd.");
                applyStatus(status);
            }
            else
            {
                FocusAbsPosNP.setState(IPS_ALERT);
                FocusAbsPosNP.apply("focuserd connection lost; retrying.");
            }
        }
    }
    SetTimer(getCurrentPollingPeriod());
}

IPState TouchFocusFocuser::MoveAbsFocuser(uint32_t targetTicks)
{
    LOGF_INFO("Absolute move requested: %u steps.", targetTicks);
    std::string reply;
    if (!sendCommand("GOTO " + std::to_string(targetTicks), reply) || reply != "OK")
        return IPS_ALERT;
    return IPS_BUSY;
}

IPState TouchFocusFocuser::MoveRelFocuser(FocusDirection direction, uint32_t ticks)
{
    const char *directionText = direction == FOCUS_INWARD ? "IN" : "OUT";
    LOGF_INFO("Relative %s move requested: %u steps.", directionText, ticks);
    std::string reply;
    const std::string command = std::string("MOVE ") + directionText + " " +
                                std::to_string(ticks);
    if (!sendCommand(command, reply) || reply != "OK")
        return IPS_ALERT;
    FocusRelPosNP[0].setValue(ticks);
    return IPS_BUSY;
}

bool TouchFocusFocuser::AbortFocuser()
{
    std::string reply;
    const bool success = sendCommand("ABORT", reply) && reply == "OK";
    if (success)
    {
        FocusAbsPosNP.setState(IPS_IDLE);
        FocusRelPosNP.setState(IPS_IDLE);
    }
    return success;
}

bool TouchFocusFocuser::SyncFocuser(uint32_t ticks)
{
    std::string reply;
    return sendCommand("SYNC " + std::to_string(ticks), reply) && reply == "OK";
}

bool TouchFocusFocuser::sendTemperatureSettings()
{
    const bool enabled = TemperatureCompensationSP[0].getState() == ISS_ON;
    char command[160];
    snprintf(command, sizeof(command), "SETTEMPCOMP %d %.3f %.3f",
             enabled ? 1 : 0, TemperatureSettingsNP[0].getValue(),
             TemperatureSettingsNP[1].getValue());
    std::string reply;
    return sendCommand(command, reply) && reply == "OK";
}

bool TouchFocusFocuser::ISNewNumber(const char *dev, const char *name,
                                    double values[], char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0 &&
        TouchFocusPresetsNP.isNameMatch(name))
    {
        TouchFocusPresetsNP.update(values, names, n);
        bool success = true;
        for (int index = 0; index < 9 && success; ++index)
        {
            std::string reply;
            const int value = static_cast<int>(std::llround(
                TouchFocusPresetsNP[index].getValue()));
            success = sendCommand("SETPRESET " + std::to_string(index + 1) +
                                  " " + std::to_string(value), reply) && reply == "OK";
        }
        TouchFocusPresetsNP.setState(success ? IPS_OK : IPS_ALERT);
        updatePresetMillimeters();
        TouchFocusPresetsNP.apply();
        return true;
    }

    if (dev && strcmp(dev, getDeviceName()) == 0 &&
        FocusRelPosNP.isNameMatch(name))
    {
        FocusRelPosNP.update(values, names, n);
        const uint32_t ticks = static_cast<uint32_t>(
            std::max(0.0, FocusRelPosNP[0].getValue()));
        const FocusDirection direction =
            FocusMotionSP[0].getState() == ISS_ON ? FOCUS_INWARD : FOCUS_OUTWARD;
        const IPState result = MoveRelFocuser(direction, ticks);
        FocusRelPosNP.setState(result);
        FocusRelPosNP.apply();
        return true;
    }

    if (dev && strcmp(dev, getDeviceName()) == 0 &&
        TemperatureSettingsNP.isNameMatch(name))
    {
        TemperatureSettingsNP.update(values, names, n);
        const bool success = sendTemperatureSettings();
        TemperatureSettingsNP.setState(success ? IPS_OK : IPS_ALERT);
        TemperatureSettingsNP.apply();
        return true;
    }
    return INDI::Focuser::ISNewNumber(dev, name, values, names, n);
}

bool TouchFocusFocuser::ISNewSwitch(const char *dev, const char *name,
                                    ISState *states, char *names[], int n)
{
    if (dev && strcmp(dev, getDeviceName()) == 0 &&
        TouchFocusPresetGotoSP.isNameMatch(name))
    {
        TouchFocusPresetGotoSP.update(states, names, n);
        int selected = -1;
        for (int index = 0; index < 9; ++index)
            if (TouchFocusPresetGotoSP[index].getState() == ISS_ON)
                selected = index;

        bool success = selected >= 0;
        if (success)
        {
            std::string reply;
            const int target = static_cast<int>(std::llround(
                TouchFocusPresetsNP[selected].getValue()));
            success = sendCommand("GOTO " + std::to_string(target), reply) && reply == "OK";
        }
        TouchFocusPresetGotoSP.reset();
        TouchFocusPresetGotoSP.setState(success ? IPS_BUSY : IPS_ALERT);
        TouchFocusPresetGotoSP.apply();
        return true;
    }

    if (dev && strcmp(dev, getDeviceName()) == 0 &&
        TemperatureCompensationSP.isNameMatch(name))
    {
        TemperatureCompensationSP.update(states, names, n);
        const bool success = sendTemperatureSettings();
        TemperatureCompensationSP.setState(success ? IPS_OK : IPS_ALERT);
        TemperatureCompensationSP.apply();
        return true;
    }
    return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
}

bool TouchFocusFocuser::saveConfigItems(FILE *fp)
{
    INDI::Focuser::saveConfigItems(fp);
    TouchFocusPresetsNP.save(fp);
    TemperatureCompensationSP.save(fp);
    TemperatureSettingsNP.save(fp);
    return true;
}

void ISGetProperties(const char *dev) { g_Focuser->ISGetProperties(dev); }
void ISNewSwitch(const char *dev, const char *name, ISState *states,
                 char *names[], int n) { g_Focuser->ISNewSwitch(dev, name, states, names, n); }
void ISNewText(const char *dev, const char *name, char *texts[],
               char *names[], int n) { g_Focuser->ISNewText(dev, name, texts, names, n); }
void ISNewNumber(const char *dev, const char *name, double values[],
                 char *names[], int n) { g_Focuser->ISNewNumber(dev, name, values, names, n); }
void ISNewBLOB(const char *dev, const char *name, int sizes[], int blobsizes[],
               char *blobs[], char *formats[], char *names[], int n)
{ g_Focuser->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n); }
void ISSnoopDevice(XMLEle *root) { g_Focuser->ISSnoopDevice(root); }
