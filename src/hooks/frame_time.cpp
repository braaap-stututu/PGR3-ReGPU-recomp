// PGR3's game clock, the world freeze and the race's milestones (PGR3 addresses, found in session 3).
//
// The main loop (sub_821B5B88, called by main sub_822A07F8) calls the app's update each frame (app 0x82523814, its
// vtable slot 8 -> sub_8229B800(game 0x82523778)). In a race (game+13 set, game+15 clear) that calls the world update
// sub_82299690(game) once per frame, which first ticks the game clock: sub_82158BC8(clock 0x82545774).
// The clock (floats): +4 scale, +12 this tick's dt, +16 game time, +24 start offset, +28 a flag that re-bases a tick
// longer than a limit (so a load doesn't show up as one huge step); game time = (real seconds - start) * scale.
//
// Freeze (PGR3_FREEZE_TIME=1, live; or FrameTimeFreeze from the grid route): after each tick of the game clock, game
// time is put back, dt is 0 and the start offset moves on by the real time that passed, so the world stops (cars,
// countdown, camera) while every frame is still drawn, and time goes on without a jump when it's let go. The route's
// freeze also sets the variable, so PGR3_FREEZE_TIME=0 lets it go.
//
// RaceStarted (Lua, sub_823595F8): the race scripts call it at GO; it sets race+24157 (race object 0x8279D6C0).
#include <atomic>
#include <cstdint>
#include <cstring>

#include <windows.h>

#include "hooks/frame_time.h"
#include "hooks/grid_test.h"
#include "hooks/telemetry.h"
#include "kernel/imports.h"
#include "live_switch.h"
#include "log.h"

namespace
{
constexpr uint32_t kGameClock = 0x82545774;

std::atomic<bool> g_freezeByRoute{ false };
std::atomic<uint64_t> g_ticks{ 0 };

float LoadBEFloat(uint32_t address)
{
    const uint32_t bits = LoadBE32(address);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
void StoreBEFloat(uint32_t address, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    StoreBE32(address, bits);
}

bool Frozen()
{
    static LiveSwitch freeze("PGR3_FREEZE_TIME");
    static bool switchWasOn = false;
    const bool switchOn = freeze;
    if (switchWasOn && !switchOn) // the switch going off also lets the route's freeze go
        g_freezeByRoute.store(false, std::memory_order_relaxed);
    switchWasOn = switchOn;
    return switchOn || g_freezeByRoute.load(std::memory_order_relaxed);
}
} // namespace

void FrameTimeFreeze(bool on)
{
    g_freezeByRoute.store(on, std::memory_order_relaxed);
    if (on)
        SetEnvironmentVariableA("PGR3_FREEZE_TIME", "1"); // so "env PGR3_FREEZE_TIME 0" is a change that lets it go
}
uint64_t FrameTimeMainFrames()
{
    return g_ticks.load(std::memory_order_relaxed);
}
uint64_t FrameTimeLastLongFrame()
{
    return 0;
}
float FrameTimeGameClock()
{
    return LoadBEFloat(kGameClock + 16);
}

// The game clock's tick (other clocks share this function; only the game clock is changed).
PPC_FUNC_IMPL(__imp__sub_82158BC8);
PPC_FUNC(sub_82158BC8)
{
    const uint32_t clock = ctx.r3.u32;
    if (clock != kGameClock)
    {
        __imp__sub_82158BC8(ctx, base);
        return;
    }
    const float before = LoadBEFloat(clock + 16);
    __imp__sub_82158BC8(ctx, base);
    g_ticks.fetch_add(1, std::memory_order_relaxed);
    const bool frozen = Frozen();
    static bool wasFrozen = false;
    if (frozen != wasFrozen)
    {
        wasFrozen = frozen;
        Log("frame time: world %s at game time %.3f s (tick %llu)", frozen ? "frozen (dt 0)" : "running again", before,
            (unsigned long long)g_ticks.load(std::memory_order_relaxed));
    }
    if (!frozen)
        return;
    const float after = LoadBEFloat(clock + 16), scale = LoadBEFloat(clock + 4);
    StoreBEFloat(clock + 16, before);
    StoreBEFloat(clock + 12, 0.0f);
    if (scale != 0.0f)
        StoreBEFloat(clock + 24, LoadBEFloat(clock + 24) + (after - before) / scale);
}

// The world update (once per frame in a race): the grid route watches the clock here.
PPC_FUNC_IMPL(__imp__sub_82299690);
PPC_FUNC(sub_82299690)
{
    __imp__sub_82299690(ctx, base);
    GridTestWorldFrame(FrameTimeGameClock());
    TelemetryWorldFrame(FrameTimeGameClock());
}

// Lua RaceStarted (GO).
PPC_FUNC_IMPL(__imp__sub_823595F8);
PPC_FUNC(sub_823595F8)
{
    __imp__sub_823595F8(ctx, base);
    Log("frame time: race started (GO) at game time %.3f s", FrameTimeGameClock());
    GridTestGo(FrameTimeGameClock());
    TelemetryGo(FrameTimeGameClock());
}
