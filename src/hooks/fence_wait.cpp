// The game's GPU fence wait without spinning (as the FM3/FM4 ports' ring_wait.cpp). D3D's block-until-fence
// (sub_823D0B48: device +10384 the fence the command processor writes, its target) loops on a poll round
// (sub_823DEE80, a 2 s timeout inside), answering non-zero while it should go on waiting. With early fences
// (gpu_host.cpp) the waits are short but many: perf on the Deck (AI race, Deck session 9) had the two at ~25% of the
// game's main thread, a core spinning on a 4-core APU whose 15 W are shared with ReGPU's draw thread (the bound).
// Now a round that says "go on" is followed by a 100 us sleep (under Wine a nanosleep, no wineserver: regpu/wine_wait.h).
// The answer and everything the round does are the game's; only the time between rounds changes.
// PGR3_FENCE_SPIN=1 (live, environment): spin as the game does (A/B).
#include <regpu/wine_wait.h>
#include <windows.h>

#include "kernel/imports.h"
#include "live_switch.h"

PPC_FUNC_IMPL(__imp__sub_823DEE80);
PPC_FUNC(sub_823DEE80)
{
    __imp__sub_823DEE80(ctx, base);
    static LiveSwitch spin("PGR3_FENCE_SPIN");
    if (ctx.r3.u32 == 0 || spin || ctx.lr != 0x823D0BE8)
        return;
    if (regpu::winewait::On())
        regpu::winewait::SleepUs(100);
    else
        SwitchToThread();
}
