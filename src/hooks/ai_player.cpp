// The player's car driven by PGR3's own race AI (for automated benchmarks), and the race-build signal the grid route
// waits for.
//
// sub_82178CB8(car, entry) runs once per car as a race is built (found from its debug tweakables "Dynamics|Car %d|
// Update Dynamics", "Game|Cars|Car%d|AutoGears", "...|AutoReverse", "...|Player/AI"). entry is the car's race entry
// (filled from the race setup's slots, which the UI scripts' AddBasicHuman / AddBasicAI write):
//   entry+64 the car's index (car+9128), entry+68 the car's type (car+9124: 1 forces AutoGears on and AutoReverse
//   off; 4 skips sub_8217E380), entry+72 (s8, -1 none) car+24144, entry+74/+75 AutoGears/AutoReverse.
// Types seen (session 3): 0 the player's car, 1 an opponent, 4 a display car (the disclaimer screen's street scene).
// PGR3_AI_PLAYER_LOG=1 (or a grid route) logs each car built with its entry.
#include <cstdlib>
#include <cstring>
#include <string>

#include "hooks/grid_test.h"
#include "kernel/imports.h"
#include "log.h"

PPC_FUNC_IMPL(__imp__sub_82178CB8);

namespace
{
bool Env(const char* name)
{
    const char* v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}
} // namespace

PPC_FUNC(sub_82178CB8)
{
    const uint32_t car = ctx.r3.u32, entry = ctx.r4.u32;
    const uint32_t index = LoadBE32(entry + 64), type = LoadBE32(entry + 68);
    if (Env("PGR3_AI_PLAYER_LOG") || Env("PGR3_GRID_TEST") || getenv("PGR3_BENCHMARK"))
    {
        std::string hex;
        char b[4];
        for (uint32_t i = 0; i < 96; i++)
            snprintf(b, sizeof(b), "%02X", *GuestPtr<uint8_t>(entry + i)), hex += b, hex += (i % 4 == 3 ? " " : "");
        Log("ai player: building car %08X: index %u type %u entry %08X: %s", car, index, type, entry, hex.c_str());
    }
    GridTestCarBuilt(index, type);
    // PGR3_AI_PLAYER=1: the player's car (type 0) is built as an opponent's (type 1), so the race AI drives it for the
    // whole race while the HUD, chase camera, position and results still follow it (checked in session 3: 1st at
    // 119 mph on lap 1). PGR3_AI_PLAYER_TYPE=<from>:<to> (testing) builds a car of type <from> as type <to>.
    uint32_t from = 0, to = 1;
    const char* swap = getenv("PGR3_AI_PLAYER_TYPE");
    const bool swapOn = swap ? sscanf(swap, "%u:%u", &from, &to) == 2 : Env("PGR3_AI_PLAYER");
    if (swapOn && type == from)
    {
        StoreBE32(entry + 68, to);
        __imp__sub_82178CB8(ctx, base);
        StoreBE32(entry + 68, type);
        Log("ai player: car %08X (index %u) built as type %u instead of %u", car, index, to, from);
        return;
    }
    __imp__sub_82178CB8(ctx, base);
}
