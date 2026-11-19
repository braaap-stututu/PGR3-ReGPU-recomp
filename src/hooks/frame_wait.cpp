// PGR3's own one-frame-in-flight throttle (session 3, the PGR4 port's present_wait idea, PGR3 addresses).
//
// After Present (sub_823CADB8) the frame function sub_821507A0 checks an option byte (0x825445D8): if it is set and the
// fence it saved after the previous frame (0x82501D04) hasn't passed, it blocks on that fence (sub_823D0D50 ->
// sub_823D0B48, D3D's spinning block-on-fence; call at 0x821508DC). Then it inserts a new fence and saves it. With
// ReGPU's early fences the fence passes when the command processor reaches it, so the game's main thread spent ~55%
// of the frozen grid spinning there while the command processor and the draw thread took turns with it.
//
// PGR3_NO_FRAME_WAIT=1 (live): only that call is skipped. Every other block-on-fence (the ones guarding memory the
// game is about to reuse) still waits.
#include <cstdlib>

#include "kernel/imports.h"
#include "live_switch.h"

PPC_FUNC_IMPL(__imp__sub_823D0D50);

PPC_FUNC(sub_823D0D50)
{
    constexpr uint32_t kFrameWaitReturn = 0x821508E0; // after the call at 0x821508DC in sub_821507A0
    static LiveSwitch noFrameWait("PGR3_NO_FRAME_WAIT");
    if (ctx.lr == kFrameWaitReturn && noFrameWait)
        return;
    __imp__sub_823D0D50(ctx, base);
}
