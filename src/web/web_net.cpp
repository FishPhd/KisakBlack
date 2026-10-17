// Web replacement for src/win32/win_net.cpp.
//
// Bots and the in-process listen client use the engine's NA_LOOPBACK queues.
// Remote NA_IP datagrams are bridged to the page's WebRTC channels or relay, keeping
// the engine's NET_* contract, sequencing and reliable-message handling intact.

#include <win32/win_net.h>
#include <win32/win_main.h>
#include <qcommon/net_chan_mp.h>
#include <qcommon/msg_mp.h>
#include <qcommon/cmd.h>
#include <qcommon/common.h>
#include <universal/q_shared.h>
#include <universal/dvar.h>
#include <qcommon/threads.h>
#include <client_mp/sv_client_mp.h>
#include <server_mp/sv_main_mp.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <deque>
#include <condition_variable>
#include <chrono>

#include <emscripten.h>

const dvar_t *ip;
const dvar_t *port;

const dvar_t *net_noudp;
const dvar_t *net_socksEnabled;
const dvar_t *net_socksServer;
const dvar_t *net_socksPort;
const dvar_t *net_socksUsername;
const dvar_t *net_socksPassword;

int numIP;
unsigned __int8 localIP[16][4];
int networkingEnabled;
int cl_nat_overflow_detected;
int g_debugServer;
int g_debugClient;

void __cdecl NET_Sleep(unsigned int msec)
{
    Sleep(msec);
}

const char *__cdecl NET_ErrorString()
{
    return "web datagram transport error";
}

int __cdecl Sys_StringToSockaddr(const char *, sockaddr *)
{
    return 0;
}

void __cdecl SockadrToNetadr(sockaddr *, netadr_t *)
{
}

// Two-tab relay. Loopback (listen server + bots) never reaches here. Real NA_IP packets are
// queued for the page, which forwards them on the WebSocket in serve.mjs (/relay).
namespace
{
struct RelayPkt
{
    netadr_t adr;
    int len;
    unsigned char data[4096];
};
std::mutex s_netMu;
std::condition_variable s_netReady;
std::deque<RelayPkt> s_in;
std::deque<RelayPkt> s_out;

unsigned short RelayPort()
{
    const unsigned short host = 3074;
    return (unsigned short)((host >> 8) | (host << 8));
}
} // namespace

void __cdecl NET_SleepUntilPacket(unsigned int msec)
{
    std::unique_lock<std::mutex> lock(s_netMu);
    s_netReady.wait_for(lock, std::chrono::milliseconds(msec), [] { return !s_in.empty(); });
}

extern "C"
{
EMSCRIPTEN_KEEPALIVE void Kisak_NetPush(int ip0, int ip1, int ip2, int ip3, int port, const unsigned char *data, int len)
{
    if (!data || len <= 0 || len > (int)sizeof(RelayPkt::data))
        return;
    RelayPkt q{};
    q.adr.type = NA_IP;
    q.adr.ip[0] = (unsigned char)ip0;
    q.adr.ip[1] = (unsigned char)ip1;
    q.adr.ip[2] = (unsigned char)ip2;
    q.adr.ip[3] = (unsigned char)ip3;
    q.adr.port = (unsigned short)port;
    q.len = len;
    memcpy(q.data, data, (size_t)len);
    std::lock_guard<std::mutex> lk(s_netMu);
    if (s_in.size() < 256)
    {
        s_in.push_back(q);
        s_netReady.notify_one();
    }
}

EMSCRIPTEN_KEEPALIVE int Kisak_NetDrain(unsigned char *out, int cap)
{
    std::lock_guard<std::mutex> lk(s_netMu);
    if (s_out.empty() || !out)
        return 0;
    RelayPkt q = s_out.front();
    if (cap < 6 + q.len)
        return 0;
    s_out.pop_front();
    out[0] = q.adr.ip[0];
    out[1] = q.adr.ip[1];
    out[2] = q.adr.ip[2];
    out[3] = q.adr.ip[3];
    out[4] = (unsigned char)(q.adr.port & 255);
    out[5] = (unsigned char)(q.adr.port >> 8);
    memcpy(out + 6, q.data, (size_t)q.len);
    return 6 + q.len;
}
}

int __cdecl Sys_StringToAdr(const char *s, netadr_t *a)
{
    if (!s || !a)
        return 0;
    memset(a, 0, sizeof(*a));
    if (!I_stricmp(s, "localhost") || !I_stricmp(s, "loopback"))
    {
        a->type = NA_LOOPBACK;
        return 1;
    }
    // "relay" and 127.0.0.2 are the other tab (the host) on the page's WebSocket room.
    int b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    if (!I_stricmp(s, "relay") || !I_strnicmp(s, "relay:", 6))
    {
        b0 = 127;
        b1 = 0;
        b2 = 0;
        b3 = 2;
    }
    else if (sscanf(s, "%d.%d.%d.%d", &b0, &b1, &b2, &b3) != 4)
        return 0;
    if ((unsigned)b0 > 255 || (unsigned)b1 > 255 || (unsigned)b2 > 255 || (unsigned)b3 > 255)
        return 0;
    a->type = NA_IP;
    a->ip[0] = (unsigned char)b0;
    a->ip[1] = (unsigned char)b1;
    a->ip[2] = (unsigned char)b2;
    a->ip[3] = (unsigned char)b3;
    a->port = RelayPort();
    return 1;
}

int __cdecl Sys_GetPacket(netadr_t *from, msg_t *msg)
{
    if (!from || !msg || !msg->data)
        return 0;
    std::lock_guard<std::mutex> lk(s_netMu);
    if (s_in.empty())
        return 0;
    RelayPkt q = s_in.front();
    s_in.pop_front();
    if (q.len > msg->maxsize)
        return 0;
    *from = q.adr;
    memcpy(msg->data, q.data, (size_t)q.len);
    msg->cursize = q.len;
    msg->readcount = 0;
    msg->bit = 0;
    return 1;
}

char __cdecl Sys_SendPacket(unsigned int len, unsigned __int8 *data, netadr_t to)
{
    if (to.type != NA_IP || !data || !len || len > sizeof(RelayPkt::data))
        return 1;
    // 127.0.0.1 is this tab. 127.0.0.2+ is the other tab on the relay.
    if (to.ip[0] == 127 && to.ip[1] == 0 && to.ip[2] == 0 && to.ip[3] <= 1)
        return 1;
    RelayPkt q{};
    q.adr = to;
    q.len = (int)len;
    memcpy(q.data, data, len);
    bool notify = false;
    {
        std::lock_guard<std::mutex> lk(s_netMu);
        if (s_out.size() < 256)
        {
            notify = s_out.empty();
            s_out.push_back(q);
        }
    }
    if (notify)
        MAIN_THREAD_ASYNC_EM_ASM({ if (Module.kisakPumpNetwork) Module.kisakPumpNetwork(); });
    return 1;
}

bool __cdecl Sys_IsLANAddress_IgnoreSubnet(netadr_t adr)
{
    if (adr.type == NA_LOOPBACK || adr.type == NA_BOT)
        return true;
    if (adr.type != NA_IP)
        return false;
    if (adr.ip[0] == 10 || adr.ip[0] == 127)
        return true;
    if (adr.ip[0] == 169 && adr.ip[1] == 254)
        return true;
    if (adr.ip[0] == 172 && (adr.ip[1] & 0xF0) == 0x10)
        return true;
    return adr.ip[0] == 192 && adr.ip[1] == 168;
}

int __cdecl Sys_IsLANAddress(netadr_t adr)
{
    return Sys_IsLANAddress_IgnoreSubnet(adr) ? 1 : 0;
}

void __cdecl Sys_ShowIP()
{
    Com_Printf(16, "IP: loopback and browser virtual peers\n");
}

// The relay reuses a virtual address as soon as the browser socket closes. The match
// slot stays until sv_timeout, and every peer uses port 3074, so the next join is
// rejected with EXE_ERR_QPORT. The page runs this when that socket leaves.
static cmd_function_s Web_DropAdr_f_VAR;
static void Web_DropAdr_f()
{
    if (!com_sv_running || !com_sv_running->current.enabled || !svs.clients || !com_maxclients)
        return;
    if (Cmd_Argc() < 2)
    {
        Com_Printf(16, "dropadr <ip>\n");
        return;
    }
    netadr_t adr;
    if (!Sys_StringToAdr(Cmd_Argv(1), &adr))
    {
        Com_Printf(16, "dropadr: bad address\n");
        return;
    }
    for (int i = 0; i < com_maxclients->current.integer; ++i)
    {
        client_t *cl = &svs.clients[i];
        if (cl->header.state < CS_CONNECTED)
            continue;
        if (cl->header.netchan.remoteAddress.type == NA_LOOPBACK)
            continue;
        if (!NET_CompareBaseAdr(adr, cl->header.netchan.remoteAddress))
            continue;
        Com_Printf(16, "dropadr: dropping client %d (%s)\n", i, cl->name);
        // The browser socket is already gone, so there is nobody to notify.
        SV_DropClient(cl, "EXE_DISCONNECTED", false, true);
        return;
    }
}

void __cdecl NET_OpenIP()
{
    ip = _Dvar_RegisterString("net_ip", "127.0.0.1", 0x20u, "Network IP Address");
    port = _Dvar_RegisterInt("net_port", 3074, 0, 0xFFFF, 0x20u, "Network port");
    static bool dropAdrRegistered = false;
    if (!dropAdrRegistered)
    {
        Cmd_AddCommandInternal("dropadr", Web_DropAdr_f, &Web_DropAdr_f_VAR);
        dropAdrRegistered = true;
    }
    Com_Printf(16, "Networking: loopback plus WebRTC/relay datagrams\n");
}

int __cdecl dwPlatformInit(bdNetStartParams *)
{
    return 0;
}

void __cdecl NET_SocketPool_Init()
{
}

void __cdecl Sys_CheckForNATOverflow()
{
}

int __cdecl Sys_SocketPool_GetPacket(netadr_t *, msg_t *)
{
    return 0;
}

bool __cdecl NET_GetDvars()
{
    bool modified = false;
    if (net_noudp)
        modified = net_noudp->modified;
    net_noudp = _Dvar_RegisterBool("net_noudp", 0, 0x21u, "Disable UDP");
    if (net_socksEnabled && net_socksEnabled->modified)
        modified = true;
    net_socksEnabled = _Dvar_RegisterBool("net_socksEnabled", 0, 0x21u, "Enable network sockets");
    if (net_socksServer && net_socksServer->modified)
        modified = true;
    net_socksServer = _Dvar_RegisterString("net_socksServer", (char *)"", 0x21u, "Network socket server");
    if (net_socksPort && net_socksPort->modified)
        modified = true;
    net_socksPort = _Dvar_RegisterInt("net_socksPort", 1080, 0, 0xFFFF, 0x21u, "Network socket port");
    if (net_socksUsername && net_socksUsername->modified)
        modified = true;
    net_socksUsername = _Dvar_RegisterString("net_socksUsername", (char *)"", 0x21u, "Network socket username");
    if (net_socksPassword && net_socksPassword->modified)
        modified = true;
    net_socksPassword = _Dvar_RegisterString("net_socksPassword", (char *)"", 0x21u, "Network socket password");
    return modified;
}

void __cdecl NET_Config(int enableNetworking)
{
    const bool modified = NET_GetDvars();
    if (net_noudp->current.enabled)
        enableNetworking = 0;
    if (enableNetworking != networkingEnabled || modified)
    {
        networkingEnabled = enableNetworking;
        if (enableNetworking && !net_noudp->current.enabled)
            NET_OpenIP();
    }
}

void __cdecl NET_Restart()
{
    NET_Config(networkingEnabled);
}

void __cdecl NET_InitDebug()
{
    g_debugClient = 0;
    g_debugServer = 0;
}

int NET_InitDebugStreams()
{
    return 0;
}

void __cdecl NET_Init()
{
    NET_GetDvars();
    NET_Config(1);
    NET_InitDebug();
}

void __cdecl Sys_Listen_f()
{
    Com_Printf(16, "net_listen: not available in the web build\n");
}

void __cdecl Sys_Connect_f()
{
    Com_Printf(16, "net_connect: not available in the web build\n");
}

unsigned int __cdecl NET_TCPIPSocket(char *, int, int)
{
    return 0;
}

int __cdecl NET_Select(unsigned int)
{
    return 0;
}

// ---- remote debug channel: never active ------------------------------------------------------

void __cdecl Sys_DebugSocketError(const char *) {}
int __cdecl Sys_IsRemoteDebugServer() { return 0; }
char __cdecl Sys_StartRemoteDebugServer() { return 0; }
int __cdecl Sys_IsRemoteDebugClient() { return 0; }
void __cdecl NET_ShutdownDebug() {}
void __cdecl NET_RestartDebug() {}
int __cdecl Sys_ReadDebugSocketData(char *, int, int) { return 0; }
void Sys_SendDebugReadBytesInternal() {}
void __cdecl Sys_SendDebugReadBytes(int) {}
int __cdecl Sys_ReadDebugSocketInt() { return 0; }
char *__cdecl Sys_ReadDebugSocketString() { static char empty[1] = {0}; return empty; }
void __cdecl Sys_WriteDebugSocketData(unsigned __int8 *, int) {}
void __cdecl Sys_DebugSend(int, const char *, int, const char *) {}
bool __cdecl Sys_DebugCanSend() { return false; }
void __cdecl Sys_WriteDebugSocketInt(int) {}
void __cdecl Sys_FlushDebugSocketData() {}
bool __cdecl Sys_DebugSocketReady(int) { return false; }
void __cdecl Sys_WriteDebugSocketMessageType(unsigned __int8) {}
void __cdecl Sys_EndWriteDebugSocket() {}
void __cdecl Sys_WriteDebugSocketString(char *) {}
int __cdecl Sys_UpdateDebugSocket() { return 0; }
int __cdecl Sys_ReadDebugSocketMessageType(unsigned __int8 *, int) { return 0; }
void __cdecl Sys_AckDebugSocket() {}
