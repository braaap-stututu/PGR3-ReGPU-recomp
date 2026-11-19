#pragma once
#include <cstdint>

// Test grid route (src/hooks/grid_test.cpp, settings grid_test=1 / PGR3_GRID_TEST=1, or PGR3_BENCHMARK=<seconds>):
// the 360 pad buttons the route holds for player 1 this poll (0: none). The route presses A and START from boot
// until the race's cars are built.
uint16_t GridTestButtons();
// Called once per car as a race is built (hooks/ai_player.cpp, sub_82178CB8): the route stops pressing.
void GridTestCarBuilt(uint32_t carIndex, uint32_t type);
// Each world update in a race (frame_time.cpp, sub_82299690), with the game clock's time.
void GridTestWorldFrame(float gameTime);
// The race's GO (Lua RaceStarted, frame_time.cpp).
void GridTestGo(float gameTime);
