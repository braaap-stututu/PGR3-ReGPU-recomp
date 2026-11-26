// Telemetry out (session 7): the player's car sent over UDP each frame of a race in Forza Motorsport 7's "Data Out"
// layout (the 311-byte "Dash" packet; telemetry_format=fh: Forza Horizon 4/5's 324 bytes), so the tools that read
// Forza's (SimHub, motion rigs, dash apps, shakers) read PGR3 as they are.
//
// Settings: settings.ini telemetry=<ip>:<port> (or PGR3_TELEMETRY; e.g. 127.0.0.1:5300; empty/0/off: none),
// telemetry_format=fm7|fh, telemetry_hz (packets a second at most, default 60; 0 = every world update).
//
// The car (PGR3's own layout, found in session 7 by sampling the player's car object while the race AI drove it,
// tools in NOTES.md). Car objects are 0x5EB0 bytes, static (the player's, index 0, at 0x82552F10); the car-build hook
// (ai_player.cpp) hands us the player's. Floats big-endian.
//   +0x10 orientation, three rows of (x, y, z, 0): row 0 the car's LEFT, row 1 up, row 2 BACK (it moves along -row 2;
//         the front wheels sit at -row 2, wheel 0 at +row 0)
//   +0x40 position (world, metres, y up)          +0x50 linear momentum (= mass x velocity)   +0x60 angular momentum
//   +0xA0 velocity (world, m/s)                    +0xB0 angular velocity (world, rad/s)        +0xE0 mass (kg)
//   +0x2340 speed (m/s)                            +0x2350 acceleration (world, m/s^2)
//   +0x2588 idle rpm (900 on the tested car)       +0x2610 redline rpm (8000)  +0x2614 gear count  +0x2620... ratios
//   +0x2674 throttle 0..1, +0x2678 brake 0..1, +0x267C steering -1 (left) .. 1 (right): the driver's (or AI's) input
//   +0x5B20 engine speed (rad/s)   +0x5B3E gear (byte, 1..6)   +0x5B40 the gear's overall ratio (with the final drive)
//   wheels at +0x55C0 + i * 0x160 (0 front left, 1 front right, 2 rear left, 3 rear right):
//     +0x08 steer angle (rad, front)   +0x0C slip angle?   +0x10 combined slip? (rises under braking)   +0x14 load (N)
//     +0x34 spin (rad/s)   +0xC0 hub position (world)   +0xE0 wheel centre (world; its height in the car's frame is
//     the suspension's travel, ~25 cm on the tested car)   +0x114 surface type (int)
//   +0x5D9C race position, 0-based (0 = leading)
// The race (object 0x8279D6C0; session 7, sampled over a 3-lap AI race): +0x5E3C the race clock (seconds), and one
// block of 0x7C0 bytes per car from +0x148, in car order (index from the car build: the player's is 0):
//   +0x000 lap counter (0 before the start line, 1 on lap 1, ..., laps + 1 when finished)   +0x1B0 sector (0-2)
//   +0x2DC lap length (m, 2754 in London)   +0x518 race distance (m)   +0x524 distance into the lap (m)
// Lap times are timed here from the race clock at each lap-counter change (none found stored).
// Not found yet (sent as 0): fuel, tyre temperatures, boost, power, torque, slip ratio.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "hooks/frame_time.h"
#include "kernel/imports.h"
#include "log.h"
#include "settings.h"

namespace
{
std::atomic<uint32_t> g_playerCar{ 0 };
std::atomic<uint32_t> g_playerIndex{ 0 };
constexpr uint32_t kRace = 0x8279D6C0;
// Lap timing (from the race clock at lap-counter changes).
uint32_t g_lapSeen = 0;
float g_lapStart = 0, g_lastLap = 0, g_bestLap = 0;
float g_goTime = -1.0f;

float F(uint32_t a)
{
    const uint32_t b = LoadBE32(a);
    float f;
    memcpy(&f, &b, 4);
    return std::isfinite(f) ? f : 0.0f;
}
struct V3
{
    float x = 0, y = 0, z = 0;
};
V3 Vec(uint32_t a) { return { F(a), F(a + 4), F(a + 8) }; }
V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Neg(V3 a) { return { -a.x, -a.y, -a.z }; }

struct Sender
{
    SOCKET s = INVALID_SOCKET;
    sockaddr_in to{};
    bool horizon = false;
    double minGap = 1.0 / 60.0;
    bool ok = false;
};
Sender& Out()
{
    static Sender out = [] {
        Sender o;
        const char* env = getenv("PGR3_TELEMETRY");
        std::string target = env ? env : GetSettings().Extra("telemetry");
        if (target.empty() || target == "0" || target == "off")
            return o;
        std::string host = "127.0.0.1";
        int port = 5300;
        if (const size_t colon = target.rfind(':'); colon != std::string::npos)
            host = target.substr(0, colon), port = atoi(target.c_str() + colon + 1);
        else if (target.find('.') == std::string::npos)
            port = atoi(target.c_str());
        else
            host = target;
        const char* fmtEnv = getenv("PGR3_TELEMETRY_FORMAT");
        const std::string fmt = fmtEnv ? fmtEnv : GetSettings().Extra("telemetry_format");
        o.horizon = fmt == "fh" || fmt == "fh4" || fmt == "fh5";
        const char* hzEnv = getenv("PGR3_TELEMETRY_HZ");
        const std::string hz = hzEnv ? hzEnv : GetSettings().Extra("telemetry_hz");
        const double rate = hz.empty() ? 60.0 : atof(hz.c_str());
        o.minGap = rate > 0 ? 1.0 / rate : 0.0;
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0 || port <= 0 || port > 65535)
        {
            Log("telemetry: can't send to '%s'", target.c_str());
            return o;
        }
        o.to.sin_family = AF_INET;
        o.to.sin_port = htons(uint16_t(port));
        if (inet_pton(AF_INET, host.c_str(), &o.to.sin_addr) != 1)
        {
            Log("telemetry: '%s' is not an IPv4 address", host.c_str());
            return o;
        }
        o.s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        o.ok = o.s != INVALID_SOCKET;
        Log("telemetry: %s packets to %s:%d, %s", o.horizon ? "Forza Horizon (324-byte)" : "Forza Motorsport 7 (311-byte)",
            host.c_str(), port, o.minGap > 0 ? (std::to_string(int(rate + 0.5)) + " a second at most").c_str() : "every frame");
        return o;
    }();
    return out;
}

// Little-endian packet writer.
struct Packet
{
    uint8_t b[324] = {};
    size_t n = 0;
    void f(float v) { memcpy(b + n, &v, 4), n += 4; }
    void i(int32_t v) { memcpy(b + n, &v, 4), n += 4; }
    void u(uint32_t v) { memcpy(b + n, &v, 4), n += 4; }
    void u16(uint16_t v) { memcpy(b + n, &v, 2), n += 2; }
    void u8(uint8_t v) { b[n++] = v; }
    void s8(int8_t v) { b[n++] = uint8_t(v); }
};

// Each wheel's suspension: the wheel centre's height in the car's frame, against the lowest and highest seen.
struct Travel
{
    float lo = 1e9f, hi = -1e9f;
};
Travel g_travel[4];
double g_distance = 0, g_lastSend = 0, g_lastTime = 0;

double Now()
{
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(freq.QuadPart);
}
} // namespace

void TelemetryCarBuilt(uint32_t car, uint32_t index, uint32_t type)
{
    if (type == 0)
    {
        g_playerCar.store(car, std::memory_order_relaxed);
        g_playerIndex.store(index < 8 ? index : 0, std::memory_order_relaxed);
        g_lapSeen = 0, g_lapStart = 0, g_lastLap = 0, g_bestLap = 0;
        g_goTime = -1.0f;
        g_distance = 0;
        for (Travel& t : g_travel)
            t = {};
    }
}
void TelemetryGo(float gameTime) { g_goTime = gameTime; }

void TelemetryWorldFrame(float gameTime)
{
    Sender& o = Out();
    const uint32_t car = g_playerCar.load(std::memory_order_relaxed);
    if (!o.ok || !car)
        return;
    const double now = Now();
    const double step = g_lastTime > 0 ? std::min(0.25, now - g_lastTime) : 0.0;
    g_lastTime = now;
    const float speed = F(car + 0x2340);
    g_distance += speed * step;
    // At most telemetry_hz a second, on a steady clock (the next due time moves on by the gap, not to now, so 86 frames
    // a second still give 60 packets).
    if (o.minGap > 0)
    {
        if (now < g_lastSend)
            return;
        g_lastSend = std::max(g_lastSend + o.minGap, now - o.minGap);
    }

    const V3 left = Vec(car + 0x10), up = Vec(car + 0x20), back = Vec(car + 0x30);
    const V3 right = Neg(left), fwd = Neg(back);
    const V3 pos = Vec(car + 0x40), vel = Vec(car + 0xA0), ang = Vec(car + 0xB0), acc = Vec(car + 0x2350);
    auto local = [&](V3 w) { return V3{ Dot(w, right), Dot(w, up), Dot(w, fwd) }; };
    const V3 lv = local(vel), la = local(acc), lw = local(ang);
    constexpr float kRadsToRpm = 60.0f / (2.0f * 3.14159265f);
    const float rpm = F(car + 0x5B20) * kRadsToRpm;
    const float idle = F(car + 0x2588), redline = F(car + 0x2610);
    const float yaw = atan2f(fwd.x, fwd.z), pitch = asinf(std::clamp(fwd.y, -1.0f, 1.0f)), roll = asinf(std::clamp(right.y, -1.0f, 1.0f));

    float travel[4], norm[4], spin[4], slipAngle[4], slipCombined[4];
    for (int w = 0; w < 4; w++)
    {
        const uint32_t wb = car + 0x55C0 + w * 0x160;
        const float h = Dot(Sub(Vec(wb + 0xE0), pos), up);
        Travel& t = g_travel[w];
        t.lo = std::min(t.lo, h), t.hi = std::max(t.hi, h);
        travel[w] = h - t.lo;
        norm[w] = t.hi - t.lo > 0.02f ? (h - t.lo) / (t.hi - t.lo) : 0.5f;
        spin[w] = F(wb + 0x34);
        slipAngle[w] = F(wb + 0x0C);
        slipCombined[w] = F(wb + 0x10);
    }

    Packet p;
    // Sled
    p.i(gameTime > 0 ? 1 : 0); // IsRaceOn
    p.u(GetTickCount());
    p.f(redline > 0 ? redline : 8000.0f);
    p.f(idle > 0 ? idle : 900.0f);
    p.f(rpm);
    p.f(la.x), p.f(la.y), p.f(la.z);
    p.f(lv.x), p.f(lv.y), p.f(lv.z);
    p.f(lw.x), p.f(lw.y), p.f(lw.z);
    p.f(yaw), p.f(pitch), p.f(roll);
    for (float v : norm) p.f(v);
    for (int w = 0; w < 4; w++) p.f(0.0f); // tyre slip ratio (not found)
    for (float v : spin) p.f(v);
    for (int w = 0; w < 4; w++) p.i(0);    // on rumble strip
    for (int w = 0; w < 4; w++) p.f(0.0f); // puddle depth
    for (int w = 0; w < 4; w++) p.f(0.0f); // surface rumble
    for (float v : slipAngle) p.f(v);
    for (float v : slipCombined) p.f(v);
    for (float v : travel) p.f(v);
    p.i(0), p.i(0), p.i(0), p.i(0), p.i(0); // car ordinal, class, PI, drivetrain, cylinders
    if (o.horizon)
        for (int k = 0; k < 3; k++)
            p.u(0); // Forza Horizon's 12 extra bytes
    // Dash
    p.f(pos.x), p.f(pos.y), p.f(pos.z);
    p.f(speed);
    p.f(0.0f), p.f(0.0f);                     // power, torque
    for (int w = 0; w < 4; w++) p.f(0.0f);    // tyre temperatures
    p.f(0.0f), p.f(1.0f);                     // boost, fuel
    // The race: lap, position, distance, times.
    const uint32_t block = kRace + 0x148 + g_playerIndex.load(std::memory_order_relaxed) * 0x7C0;
    const uint32_t lapCounter = LoadBE32(block);
    const float raceClock = F(kRace + 0x5E3C);
    if (lapCounter != g_lapSeen)
    {
        if (lapCounter == g_lapSeen + 1 && g_lapSeen >= 1)
        {
            g_lastLap = raceClock - g_lapStart;
            if (g_lastLap > 0 && (g_bestLap <= 0 || g_lastLap < g_bestLap))
                g_bestLap = g_lastLap;
        }
        if (lapCounter < g_lapSeen)
            g_lastLap = g_bestLap = 0; // (a restart)
        g_lapSeen = lapCounter;
        g_lapStart = raceClock;
    }
    const float raceDistance = F(block + 0x518);
    p.f(raceDistance > 0 ? raceDistance : float(g_distance));
    p.f(g_bestLap), p.f(g_lastLap);
    p.f(lapCounter >= 1 ? std::max(0.0f, raceClock - g_lapStart) : 0.0f); // current lap
    p.f(std::max(0.0f, raceClock));                                     // race time
    p.u16(uint16_t(lapCounter >= 1 ? lapCounter - 1 : 0));               // lap (0 = the first)
    p.u8(uint8_t(std::min<uint32_t>(LoadBE32(car + 0x5D9C), 254) + 1)); // position (1 = leading)
    auto byte01 = [](float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    p.u8(byte01(F(car + 0x2674)));            // throttle
    p.u8(byte01(F(car + 0x2678)));            // brake
    p.u8(0);                                  // clutch
    p.u8(0);                                  // handbrake
    p.u8(*GuestPtr<uint8_t>(car + 0x5B3E));   // gear
    p.s8(int8_t(std::clamp(F(car + 0x267C), -1.0f, 1.0f) * 127.0f));
    p.s8(0), p.s8(0);                         // driving line, AI brake difference
    sendto(o.s, reinterpret_cast<const char*>(p.b), int(p.n), 0, reinterpret_cast<const sockaddr*>(&o.to), sizeof(o.to));
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        Log("telemetry: first packet (%zu bytes): car %08X, %.1f m/s, %.0f rpm, gear %u", p.n, car, speed, rpm,
            *GuestPtr<uint8_t>(car + 0x5B3E));
    }
}
