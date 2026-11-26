#pragma once
#include <cstdint>

// Triple screen (ReGPU REGPU_TRIPLE; FM3's, ported): see triple_screen.cpp.
bool TripleOn();
// The race camera's field of view (vertical, radians) widened so its frustum covers all three screens, and the screens'
// views handed to ReGPU (src/hooks/vr_camera.cpp: the camera update writes the result into the camera, + 0x40).
float TripleCameraFov(float ownFov);
