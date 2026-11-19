// Placeholders for the FM3 runtime's game hooks that PGR3 doesn't have addresses for yet. The runtime
// (gpu_host.cpp, imports_xam.cpp) calls them; each does nothing until a PGR3 version is written
// (with addresses found in PGR3's own XEX: never FM3's).
#include <cstdint>

#include "hooks/frame_time.h"
#include "hooks/grid_test.h"

// Frame pacing of the main loop (FM3 Q24). PGR3's main loop and physics step are not found yet (PLAN Phase 6).
void FrameTimeSetPacing(uint32_t, uint32_t, uint32_t) {}

// Test grid route (FM3 grid_test.cpp): written for PGR3 in Phase 4.
bool GridTestPressA()
{
    return false;
}

// FM3's one-guest-tile hook: off.
bool OneTileEnabled()
{
    return false;
}
