// Placeholders for the FM3 runtime's game hooks that PGR3 doesn't have addresses for yet. The runtime
// (gpu_host.cpp) calls them; each does nothing until a PGR3 version is written
// (with addresses found in PGR3's own XEX: never FM3's).
#include <cstdint>

#include "hooks/frame_time.h"

// Frame pacing of the main loop (FM3 Q24). PGR3's main loop and physics step are not found yet (PLAN Phase 6).
void FrameTimeSetPacing(uint32_t, uint32_t, uint32_t) {}

// FM3's one-guest-tile hook: off.
bool OneTileEnabled()
{
    return false;
}
