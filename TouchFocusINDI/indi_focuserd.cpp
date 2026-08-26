#include <indifocuser.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <memory>
#include <stdio.h>

#define MAX_MSG 128

class Focuserd : public INDI::Focuser
{
public:
    Focuserd();
    virtual ~Focuserd();

    virtual bool Connect() override;
    virtual bool Disconnect() override;
    virtual void TimerHit() override;
    const char *getDefaultName()
    {
        printf("getDefaultName called, returning: Focuserd\n");
        return "Focuserd";
    }

    INumber FocusAbsPosN[1];
    INumberVectorProperty FocusAbsPosNP;

    // --- PRESETY ---
    INumber PresetN[6];
    INumberVectorProperty PresetNP;
    void ISGetProperties(const char *dev) override;
    void updateAbsPos(int pos);
    void updatePresets();
    virtual bool ISNewNumber(const char *dev, const char *name, double *vals, char *names[], int n) override;
protected:
    // INDI properties
    virtual IPState MoveAbsFocuser(uint32_t targetTicks) override;
    virtual IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
    virtual bool AbortFocuser() override;
    virtual bool SyncFocuser(uint32_t ticks) override;
    virtual bool HomeFocuser();

        // Network
    bool sendCommand(const std::string &cmd, std::string &reply);

    // config
    std::string server_ip = "127.0.0.1";
    int server_port = 7625;
    bool use_tcp = true;

    // state
    int sockfd = -1;
    int lastAbsPos = 0;
};

Focuserd::Focuserd()
{
    printf("Focuserd constructor called\n");
    printf("getDefaultName() returns: %s\n", getDefaultName());
    lastAbsPos = 0;
    IUFillNumber(&FocusAbsPosN[0], "FOCUS_ABS_POS", "Abs Pos", "%6.0f", 0, 50000, 10, 0);
    IUFillNumberVector(&FocusAbsPosNP, FocusAbsPosN, 1, getDeviceName(), "ABS_FOCUS_POSITION", "Main Control", "Focuser", IP_RW, 60, IPS_IDLE);

    // --- PRESETY ---
    for (int i = 0; i < 6; i++)
    {
        char name[16];
        snprintf(name, sizeof(name), "PRESET%d", i + 1);
        IUFillNumber(&PresetN[i], name, name, "%6.0f", 0, 50000, 1, 0);
    }
    IUFillNumberVector(&PresetNP, PresetN, 6, getDeviceName(), "PRESETS", "Presets", "Presets", IP_RW, 60, IPS_IDLE);
    PresetNP.s = IPS_OK;
    IDSetNumber(&PresetNP, nullptr);
    
    // Automatically start TimerHit
    SetTimer(1000); // Start timer after 1 second
    printf("Focuserd constructor completed, timer started\n");
    
    // Force flush stdout
    fflush(stdout);
}

Focuserd::~Focuserd()
{
    if (sockfd > 0) close(sockfd);
}

// --- Sov pipojen ---
bool Focuserd::Connect()
{
    printf("Attempting to connect to %s:%d\n", server_ip.c_str(), server_port);
    
    if (sockfd > 0) close(sockfd);
    sockfd = socket(AF_INET, use_tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        printf("Failed to create socket\n");
        return false;
    }
    
    sockaddr_in serv_addr {};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    if (use_tcp)
    {
        printf("Connecting via TCP...\n");
        if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        {
            printf("Failed to connect to focuserd.py\n");
            close(sockfd); sockfd = -1;
            return false;
        }
        printf("Successfully connected to focuserd.py\n");
    }

    // --- P�i p�ipojen� na�ti PRESETS z focuserd.py ---
    updatePresets();

    return true;
}

bool Focuserd::Disconnect()
{
    if (sockfd > 0) close(sockfd);
    sockfd = -1;
    return true;
}

// --- Aktualizace pozice ---
void Focuserd::updateAbsPos(int pos)
{
    lastAbsPos = pos;
    FocusAbsPosN[0].value = pos;
    FocusAbsPosNP.s = IPS_OK;
    IDSetNumber(&FocusAbsPosNP, nullptr);
}

// --- Na�ti aktu�ln� presety z focuserd.py ---
void Focuserd::updatePresets()
{
    std::string reply;
    if (sendCommand("LISTPRESETS", reply))
    {
        printf("updatePresets odpoved: [%s]\n", reply.c_str());
        // Odpovd ve tvaru: "PRESETS 123 456 789 ...\n"
        int p[6] = {0,0,0,0,0,0};
        if (sscanf(reply.c_str(), "PRESETS %d %d %d %d %d %d", &p[0], &p[1], &p[2], &p[3], &p[4], &p[5]) == 6)
        {
            for (int i = 0; i < 6; i++)
                PresetN[i].value = p[i];
            PresetNP.s = IPS_OK;
            IDSetNumber(&PresetNP, nullptr);
        }
    }
    else
    {
        printf("updatePresets failed - no response from focuserd.py\n");
    }
}

// --- Periodick dotaz na pozici ---
void Focuserd::TimerHit()
{
    // Automatically connect if not connected
    if (sockfd < 0)
    {
        printf("Auto-connecting to focuserd.py...\n");
        Connect();
    }
    
    std::string reply;
    if (sendCommand("GETPOS", reply))
    {
        printf("TimerHit odpoved z pythonu: [%s]\n", reply.c_str()); // <<-- toto!
        int pos = 0;
        if (sscanf(reply.c_str(), "POS %d", &pos) == 1)
            updateAbsPos(pos);
    }
    else
    {
        printf("TimerHit failed - no response from focuserd.py\n");
    }
    // Dotaz na presety (nap. pokud byly zmnny na HW)
    updatePresets();
    SetTimer(500);
}

// --- Pohyb ---
IPState Focuserd::MoveAbsFocuser(uint32_t ticks)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "GOTO %d", (int)ticks);
    printf("Posilam na focuserd.py: %s\n", buf);
    std::string reply;
    if (sendCommand(buf, reply) && reply.find("OK") != std::string::npos)
        return IPS_OK;
    printf("Neuspech nebo odpoved: [%s]\n", reply.c_str());
    return IPS_ALERT;
}

IPState Focuserd::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "MOVE %s %d", dir == FOCUS_INWARD ? "IN" : "OUT", (int)ticks);
    printf("Posilam na focuserd.py: %s\n", buf);
    std::string reply;
    if (sendCommand(buf, reply) && reply.find("OK") != std::string::npos)
        return IPS_OK;
    printf("Neuspech nebo odpoved: [%s]\n", reply.c_str());
    return IPS_ALERT;
}

bool Focuserd::AbortFocuser()
{
    std::string reply;
    return sendCommand("ABORT", reply) && reply.find("OK") != std::string::npos;
}

bool Focuserd::SyncFocuser(uint32_t ticks)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "SYNC %d", (int)ticks);
    std::string reply;
    return sendCommand(buf, reply) && reply.find("OK") != std::string::npos;
}

bool Focuserd::HomeFocuser()
{
    std::string reply;
    return sendCommand("HOME", reply) && reply.find("OK") != std::string::npos;
}

// --- Odesln pkaz ---
bool Focuserd::sendCommand(const std::string &cmd, std::string &reply)
{
    if (sockfd < 0) 
    {
        printf("sendCommand: not connected to focuserd.py\n");
        return false;
    }
    if (use_tcp)
    {
        std::string tosend = cmd + "\n";
        printf("Sending to focuserd.py: [%s]", tosend.c_str());
        if (send(sockfd, tosend.c_str(), tosend.size(), 0) < 0)
        {
            printf("sendCommand: send failed\n");
            return false;
        }
        char buf[MAX_MSG] = {};
        int n = recv(sockfd, buf, sizeof(buf)-1, 0);
        if (n > 0) 
        {
            reply.assign(buf, n);
            printf("Received from focuserd.py: [%s]", reply.c_str());
        }
        return n > 0;
    }
    else
    {
        sockaddr_in serv_addr {};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);
        sendto(sockfd, cmd.c_str(), cmd.size(), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        char buf[MAX_MSG] = {};
        socklen_t len = sizeof(serv_addr);
        int n = recvfrom(sockfd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&serv_addr, &len);
        if (n > 0) reply.assign(buf, n);
        return n > 0;
    }
}

// --- INDI: z�pis preset� z klienta ---
bool Focuserd::ISNewNumber(const char *dev, const char *name, double *vals, char *names[], int n)
{
    if (strcmp(name, "PRESETS") == 0)
    {
        bool anyChanged = false;
        for (int i = 0; i < 6 && i < n; i++)
        {
            double newVal = vals[i];
            if (PresetN[i].value != newVal)
            {
                PresetN[i].value = newVal;
                // Z�pis do focuserd.py
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "SETPRESET %d %d", i+1, (int)newVal);
                std::string reply;
                sendCommand(cmd, reply);
                anyChanged = true;
            }
        }
        if (anyChanged)
        {
            PresetNP.s = IPS_OK;
            IDSetNumber(&PresetNP, nullptr);
        }
        return true;
    }
    // Defaultn� zpracov�n� ostatn�ch ��seln�k�
    return INDI::Focuser::ISNewNumber(dev, name, vals, names, n);
}
void Focuserd::ISGetProperties(const char *dev)
{
    printf("=== ISGetProperties START ===\n");
    printf("ISGetProperties called for device: %s\n", dev ? dev : "NULL");
    printf("getDefaultName() returns: %s\n", getDefaultName());
    printf("getDeviceName() returns: %s\n", getDeviceName());
    
    // Zkusme nejdříve definovat properties
    printf("Defining PresetNP property...\n");
    defineProperty(&PresetNP);
    printf("PresetNP property defined\n");
    
    // Pak zavolej rodiovskou implementaci
    printf("Calling parent ISGetProperties...\n");
    INDI::Focuser::ISGetProperties(dev);
    printf("Parent ISGetProperties completed\n");
    
    printf("ISGetProperties completed\n");
    printf("=== ISGetProperties END ===\n");
    fflush(stdout);
}

// --- INDI boilerplate ---
#include "indidevapi.h"
#include "eventloop.h"

std::unique_ptr<Focuserd> focuserd(new Focuserd());

void ISGetProperties(const char *dev)
{
    printf("=== Global ISGetProperties START ===\n");
    printf("Global ISGetProperties called for device: %s\n", dev ? dev : "NULL");
    focuserd->ISGetProperties(dev);
    printf("=== Global ISGetProperties END ===\n");
    fflush(stdout);
}
void ISNewNumber(const char *dev, const char *name, double *vals, char *names[], int n)
{
    focuserd->ISNewNumber(dev, name, vals, names, n);
}
void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    focuserd->ISNewSwitch(dev, name, states, names, n);
}
void ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    focuserd->ISNewText(dev, name, texts, names, n);
}
// ISPoll není potřeba pro focuser
// void ISPoll(void *p)
// {
//     printf("ISPoll called\n");
//     focuserd->ISPoll(p);
// }
