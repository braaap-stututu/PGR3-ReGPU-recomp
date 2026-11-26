#pragma once
#include <cstdint>

// Telemetry out over UDP in Forza's Data Out layout (src/hooks/telemetry.cpp; settings.ini telemetry=<ip>:<port>).
void TelemetryCarBuilt(uint32_t car, uint32_t index, uint32_t type); // the car-build hook (ai_player.cpp)
void TelemetryGo(float gameTime);                                    // the race's GO (frame_time.cpp)
void TelemetryWorldFrame(float gameTime);                            // each world update in a race (frame_time.cpp)
