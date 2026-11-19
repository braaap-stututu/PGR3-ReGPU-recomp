// Test grid route and benchmark race (PGR3 version of the FM3 port's grid_test.cpp; PGR3's own addresses).
//
// grid_test=1 (settings.ini, PGR3_GRID_TEST=1) or PGR3_BENCHMARK=<seconds>:
// 1. From boot, press A and START in turn about three times a second (title "press START", then every menu step to
//    the first race is its first item: main menu > Gotham Career > event > garage > Use this car > Novice).
//    With the route's own profile (out\user-route) A/START from the title lands on a 6-car London race (Honda NSX,
//    3 laps, target 3rd) in ~13 s.
// 2. Stop once the race's cars are built (GridTestCarBuilt, from the car-build hook in ai_player.cpp; type 4, the
//    disclaimer screen's display car, doesn't count).
// 3. Grid test: freeze the world (frame_time.cpp) FreezeAt() game seconds after the race's first frame. Then
//    `Pgr3 "env PGR3_FREEZE_TIME 0"` lets the race go on (and `... 1` stops it again anywhere).
// 4. Benchmark (PGR3_BENCHMARK=N, no freeze): the race AI drives the player's car (PGR3_AI_PLAYER, set in gpu_host.cpp
//    with the light performance log). PGR3_BENCHMARK_SETTLE seconds (default 10) after GO, log "benchmark: measuring
//    N s", wait N seconds, log "benchmark: done" and close the game's window (as the user would).
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include "hooks/frame_time.h"
#include "hooks/grid_test.h"
#include "log.h"
#include "settings.h"

namespace
{
enum class Route : int { Off, Pressing, Racing };
std::atomic<Route> g_route{ Route::Off };

int BenchmarkSeconds()
{
    static const int s = getenv("PGR3_BENCHMARK") ? std::max(0, atoi(getenv("PGR3_BENCHMARK"))) : 0;
    return s;
}

BOOL CALLBACK CloseOurWindow(HWND hwnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr)
        PostMessageA(hwnd, WM_CLOSE, 0, 0); // what Process.CloseMainWindow (and the close button) does
    return TRUE;
}

// PGR3_GRID_FREEZE_AT: game seconds after the race's first frame. Measured in session 3: GO (Lua RaceStarted) comes
// at 5.16 s; at 4.6 s the chase camera has settled behind the player's car and the opponents sit on their grid spots.
// The countdown itself runs on another clock, so GO still fires during the freeze (nothing moves until it is let go).
double FreezeAt()
{
    static const double at = getenv("PGR3_GRID_FREEZE_AT") ? atof(getenv("PGR3_GRID_FREEZE_AT")) : 4.6;
    return at;
}

void StartBenchmarkClock()
{
    std::thread([] {
        const char* s = getenv("PGR3_BENCHMARK_SETTLE");
        const int settle = s ? std::max(0, atoi(s)) : 10;
        Sleep(DWORD(settle) * 1000);
        Log("benchmark: measuring %d s (from %d s after GO)", BenchmarkSeconds(), settle);
        Sleep(DWORD(BenchmarkSeconds()) * 1000);
        Log("benchmark: done");
        Sleep(1500); // (one more perf lite line, and the log written)
        EnumWindows(CloseOurWindow, 0);
    }).detach();
}
} // namespace

uint16_t GridTestButtons()
{
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    static const bool armed = [] {
        if (!GetSettings().gridTest)
            return false;
        g_route = Route::Pressing;
        if (BenchmarkSeconds())
            Log("grid test: benchmark race: pressing A/START until the race is built; the AI drives, %d s measured", BenchmarkSeconds());
        else
            Log("grid test: on: pressing A/START until the race is built");
        return true;
    }();
    if (!armed || g_route.load(std::memory_order_relaxed) != Route::Pressing)
        return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    if (ms > 240000)
    {
        g_route = Route::Off;
        Log("grid test: no race after 4 minutes: presses stopped");
        return 0;
    }
    if (ms % 300 >= 100) // 100 ms down, 200 ms up
        return 0;
    return (ms / 300) % 2 ? 0x1000 /* A */ : 0x0010 /* START */;
}

void GridTestCarBuilt(uint32_t, uint32_t type)
{
    if (type == 4) // a display car (the disclaimer screen's street scene builds one at boot), not a race
        return;
    Route expected = Route::Pressing;
    if (!g_route.compare_exchange_strong(expected, Route::Racing))
        return;
    Log("grid test: race cars being built: presses stopped");
}

void GridTestWorldFrame(float gameTime)
{
    if (g_route.load(std::memory_order_relaxed) != Route::Racing)
        return;
    static float start = -1.0f;
    static bool frozen = false;
    if (start < 0.0f)
    {
        start = gameTime;
        Log("grid test: first race frame at game time %.3f s", gameTime);
    }
    if (frozen || BenchmarkSeconds() || gameTime - start < FreezeAt())
        return;
    frozen = true;
    FrameTimeFreeze(true);
    Log("grid test: frozen on the grid %.3f s after the race's first frame (game time %.3f s)", gameTime - start, gameTime);
}

void GridTestGo(float)
{
    if (g_route.load(std::memory_order_relaxed) != Route::Racing || !BenchmarkSeconds())
        return;
    static bool started = false;
    if (started)
        return;
    started = true;
    Log("grid test: benchmark race: GO");
    StartBenchmarkClock();
}
