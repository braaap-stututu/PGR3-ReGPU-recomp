#pragma once

// PGR3's view hand-off hook (sub_82157EF8; src/hooks/vr_camera.cpp): triple screen and (not working yet) VR head
// tracking. VrCameraMainFrame is FM3's per-frame entry, unused in PGR3 (the hand-off submits the frame's pose).
void VrCameraMainFrame();
