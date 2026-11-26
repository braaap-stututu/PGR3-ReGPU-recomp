// PGR3's view hand-off: VR head tracking and triple screen (session 7; FM3's scheme, src/hooks/triple_screen.cpp and
// FM3-ReGPU's vr_camera.cpp, on PGR3's own camera).
//
// The hand-off (found in session 7 from the renderer's view block at 0x82545440): sub_82157EF8(view) runs once per
// drawn view (the main view, the mirror, ...). It stores the view at 0x8254550C, copies the view object's camera
// matrix (view + 0x20, 4x4 floats, rows: right, up, forward, position; left-handed, x right, y up, z forward) to
// 0x82545510, inverts it into the view matrix (0x82545550), builds the projection (0x825455D0) from view + 0x170 (the
// vertical field of view, radians), view + 0x16C (an aspect correction, 1.0417), the viewport (view + 8 / + 12,
// 1024 x 600) and near / far (view + 0x164 / + 0x168: 0.05 / 4000; a reversed infinite projection, z_clip = near,
// w = view z), then the view-projection (0x82545610), the frustum planes and the shader constants (camera position).
// The race's main view object is 0x82798570 (PGR3_MAIN_VIEW=<hex> to pick another). A race frame (PGR3_CAMERA_TRACE):
// shadow cascades (40043680), the car's cube map (82799560, six faces at the car), the main view three times, 2D views,
// 40043250 (a copy of the main camera), the main view again, 40043940 (2D, HUD).
//
// The scene is NOT drawn from the hand-off's matrices: the race camera object 0x828B3840 makes them (its matrix at
// + 0x50 and field of view at + 0x40, read by the render function sub_8229A548; see the camera update hook at the end).
// Changes at the hand-off reach culling, LOD and motion blur only. So triple screen widens the frustum through the
// race camera's own field of view after its update, and VR head tracking poses its matrix there (sub_822FBC58 below).
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hooks/frame_time.h"
#include "hooks/triple_screen.h"
#include "hooks/vr_camera.h"
#include "kernel/imports.h"
#include "log.h"
#include "regpu/regpu.h"

PPC_FUNC_IMPL(__imp__sub_82157EF8);

namespace
{
constexpr uint32_t kViewMatrix = 0x20, kViewFov = 0x170;
constexpr uint32_t kPlayerCar = 0x82552F10; // the player's car (index 0), its position at + 0x40

float LoadF(uint32_t a)
{
    const uint32_t b = LoadBE32(a);
    float f;
    memcpy(&f, &b, 4);
    return f;
}
void StoreF(uint32_t a, float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    StoreBE32(a, b);
}
// Motion blur and depth of field off (the debug variables "Post Processing|Motion Blur|Enable" 0x8259CC90 and
// "Post Processing|Depth Of Field|Enable" 0x8259CCB0, bool value at + 4) in triple and VR: they rebuild positions from
// the depth buffer with the hand-off's matrices, which the per-screen and per-eye views don't match (radial smears).
// PGR3_KEEP_POST_BLUR=1 keeps them.
void PostBlurOff()
{
    static const bool keep = getenv("PGR3_KEEP_POST_BLUR") != nullptr;
    if (keep)
        return;
    *GuestPtr<uint8_t>(0x8259CC94) = 0;
    *GuestPtr<uint8_t>(0x8259CCB4) = 0;
}
uint32_t MainView()
{
    static const uint32_t v = [] {
        const char* e = getenv("PGR3_MAIN_VIEW");
        return e && *e ? uint32_t(strtoul(e, nullptr, 16)) : 0x82798570u;
    }();
    return v;
}
bool VrTracked()
{
    static const bool on = [] {
        const char* v = getenv("REGPU_VR");
        return v && (strcmp(v, "cockpit") == 0 || strcmp(v, "stereo") == 0);
    }();
    return on;
}

// PGR3_VR_FAKE: a fixed head pose (no headset needed), as OpenXR would give it.
bool FakePose(regpu::vr::HeadPose& out)
{
    static float v[6] = {};
    static bool have = false;
    static DWORD lastRead = 0;
    if (GetTickCount() - lastRead > 500)
    {
        lastRead = GetTickCount();
        char buf[128] = {};
        have = GetEnvironmentVariableA("PGR3_VR_FAKE", buf, sizeof(buf)) > 0 &&
               sscanf(buf, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) >= 1;
    }
    if (!have)
        return false;
    const float d = 3.14159265f / 180.0f;
    const float cy = cosf(v[0] * d * 0.5f), sy = sinf(v[0] * d * 0.5f), cp = cosf(v[1] * d * 0.5f), sp = sinf(v[1] * d * 0.5f);
    const float cr = cosf(v[2] * d * 0.5f), sr = sinf(v[2] * d * 0.5f);
    const float qy[4] = { 0, sy, 0, cy }, qp[4] = { sp, 0, 0, cp }, qr[4] = { 0, 0, sr, cr };
    auto mul = [](const float a[4], const float b[4], float o[4]) {
        o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    };
    float t[4];
    mul(qy, qp, t);
    mul(t, qr, out.orientation);
    out.position[0] = v[3] * 0.01f, out.position[1] = v[4] * 0.01f, out.position[2] = v[5] * 0.01f;
    out.fovLeft = -0.9f, out.fovRight = 0.9f, out.fovUp = 0.9f, out.fovDown = -0.9f;
    return true;
}

// The camera matrix (rows right, up, forward, position; left-handed) posed by the head: OpenXR's pose is right-handed
// with -z forward, so in the camera's space the head's turn is Rlh = S R S and its offset S t (S = diag(1, 1, -1)). Rlh's
// columns are the head's axes in camera space: the new row k = sum_j Rlh[j][k] * row j, the position moves by
// sum_j t_j * row j.
void PoseCamera(const float in[16], const regpu::vr::HeadPose& p, float out[16])
{
    const float x = p.orientation[0], y = p.orientation[1], z = p.orientation[2], w = p.orientation[3];
    const float r[3][3] = {
        { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
        { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
        { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) },
    };
    float m[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] = r[i][j] * ((i == 2) != (j == 2) ? -1.0f : 1.0f);
    const float t[3] = { p.position[0], p.position[1], -p.position[2] };
    memcpy(out, in, sizeof(float) * 16);
    for (int k = 0; k < 3; k++)
        for (int c = 0; c < 3; c++)
            out[k * 4 + c] = m[0][k] * in[0 * 4 + c] + m[1][k] * in[1 * 4 + c] + m[2][k] * in[2 * 4 + c];
    for (int c = 0; c < 3; c++)
        out[12 + c] = in[12 + c] + t[0] * in[0 * 4 + c] + t[1] * in[1 * 4 + c] + t[2] * in[2 * 4 + c];
}

// First person: the camera within 2.5 m of the player car's centre (session 7, the race cameras cycled with LB, distance
// from the car's centre: in-car 0.76 m, the two front views 1.51 / 1.68 m, chase 5.32 / 6.45 m).
bool FirstPerson(const float cam[16])
{
    static const bool any = getenv("PGR3_VR_ANY_CAMERA") != nullptr; // (testing: every camera head-tracked)
    if (any)
        return true;
    const float dx = cam[12] - LoadF(kPlayerCar + 0x40), dy = cam[13] - LoadF(kPlayerCar + 0x44), dz = cam[14] - LoadF(kPlayerCar + 0x48);
    return dx * dx + dy * dy + dz * dz < 2.5f * 2.5f;
}

void LogViews(uint32_t view)
{
    static uint32_t seen[16] = {};
    for (uint32_t& s : seen)
    {
        if (s == view)
            return;
        if (!s)
        {
            s = view;
            Log("camera: view %08X (%ux%u, fov %.3f rad)%s", view, LoadBE32(view + 8), LoadBE32(view + 12), LoadF(view + kViewFov),
                view == MainView() ? " (the main view)" : "");
            return;
        }
    }
}

} // namespace

void VrCameraMainFrame() {}

PPC_FUNC(sub_82157EF8)
{
    const uint32_t view = ctx.r3.u32;
    LogViews(view);
    // PGR3_CAMERA_TRACE=<n> (live: debug server "env PGR3_CAMERA_TRACE 40"): the next n hand-offs logged with the
    // view's camera position and field of view (then the variable is cleared).
    static int trace = 0;
    static uint32_t polls = 0;
    if ((polls++ & 63) == 0)
    {
        char buf[16] = {};
        if (GetEnvironmentVariableA("PGR3_CAMERA_TRACE", buf, sizeof(buf)) > 0 && atoi(buf) > 0)
        {
            trace = atoi(buf);
            SetEnvironmentVariableA("PGR3_CAMERA_TRACE", nullptr);
        }
    }
    if (trace > 0)
    {
        trace--;
        Log("camera: hand-off %08X (%ux%u) fov %.3f pos %.2f %.2f %.2f fwd %.2f %.2f %.2f lr %08X", view, LoadBE32(view + 8),
            LoadBE32(view + 12), LoadF(view + kViewFov), LoadF(view + 0x50), LoadF(view + 0x54), LoadF(view + 0x58),
            LoadF(view + 0x40), LoadF(view + 0x44), LoadF(view + 0x48), uint32_t(ctx.lr));
    }
    // The main camera's views: the main view object and any other view drawn from the same camera (session 7: in a
    // race each frame hands off 82798570 three times, then 40043250, a copy with the same camera matrix and field of
    // view that the world is drawn with, then 82798570 again; only changing 82798570 left the scene as it was).
    const uint32_t mainView = MainView();
    const bool isMain = view == mainView ||
                        (LoadBE32(view + 8) == LoadBE32(mainView + 8) && LoadBE32(view + 12) == LoadBE32(mainView + 12) &&
                         LoadF(view + kViewFov) == LoadF(mainView + kViewFov) && LoadF(view + 0x50) == LoadF(mainView + 0x50) &&
                         LoadF(view + 0x54) == LoadF(mainView + 0x54) && LoadF(view + 0x58) == LoadF(mainView + 0x58));
    if (!isMain)
    {
        __imp__sub_82157EF8(ctx, base);
        return;
    }
    if (TripleOn() || VrTracked())
        PostBlurOff(); // (the triple frustum and the head pose go in at the camera update: sub_822FBC58 below)
    __imp__sub_82157EF8(ctx, base);
}

// Camera research (session 7): the per-type camera function dispatcher sub_822FB9F8 (r3 = the camera; type at +492:
// 0/1 chase, 3/4 dash, ...; position at +0, field of view at +64). PGR3_CAM_OFFSET="dx dy dz" (debug, read twice a
// second): the camera's position moved by that much (metres) after its update.
namespace
{
bool CamOffset(float off[3])
{
    static float v[3] = {};
    static bool have = false;
    static DWORD lastRead = 0;
    if (GetTickCount() - lastRead > 500)
    {
        lastRead = GetTickCount();
        char buf[96] = {};
        have = GetEnvironmentVariableA("PGR3_CAM_OFFSET", buf, sizeof(buf)) > 0 && sscanf(buf, "%f %f %f", &v[0], &v[1], &v[2]) == 3;
    }
    memcpy(off, v, sizeof(v));
    return have;
}
void LogCamera(const char* where, uint32_t cam)
{
    static uint32_t logged[16] = {};
    for (uint32_t& l : logged)
    {
        if (l == cam)
            return;
        if (!l)
        {
            l = cam;
            Log("camera: %s %08X type %u pos %.2f %.2f %.2f fov %.3f", where, cam, LoadBE32(cam + 492), LoadF(cam), LoadF(cam + 4),
                LoadF(cam + 8), LoadF(cam + 64));
            return;
        }
    }
}
} // namespace

PPC_FUNC_IMPL(__imp__sub_822FB9F8);
PPC_FUNC(sub_822FB9F8)
{
    const uint32_t cam = ctx.r3.u32;
    __imp__sub_822FB9F8(ctx, base);
    LogCamera("dispatch", cam);
    float off[3];
    if (CamOffset(off))
        for (int i = 0; i < 3; i++)
            StoreF(cam + i * 4, LoadF(cam + i * 4) + off[i]);
}

// VR head tracking (session 7). The scene is drawn from the race camera object 0x828B3840 (the render function
// sub_8229A548 reads it as 0x828B3830 + 16): its camera matrix at + 0x50 (4x4: rows right, up, forward, position;
// left-handed) and field of view at + 0x40 (radians, vertical). The camera update sub_822FBC58 rebuilds that matrix
// from the camera's position and orientation each frame (not while the world is frozen or paused: + 1208 set), so
// the head's pose goes into the matrix after the update; a matrix not rebuilt since (still the one we posed) is put back
// to the game's first, so poses never pile up. Checked on the grid by moving the camera's position after its update
// (session 7: the scene followed; changes at the view hand-off sub_82157EF8 only reached culling and motion blur).
// While the camera is within 2.5 m of the player car's centre (the in-car, bonnet and bumper views); PGR3_VR_FAKE=
// "yaw pitch roll x y z" (degrees, cm) poses without a headset. The headset's field of view goes into + 0x40.
PPC_FUNC_IMPL(__imp__sub_822FBC58);
PPC_FUNC(sub_822FBC58)
{
    const uint32_t cam = ctx.r3.u32;
    __imp__sub_822FBC58(ctx, base);
    static const uint32_t sceneCamera = [] {
        const char* e = getenv("PGR3_SCENE_CAMERA");
        return e && *e ? uint32_t(strtoul(e, nullptr, 16)) : 0x828B3840u;
    }();
    if (cam != sceneCamera)
        return;
    constexpr uint32_t kMatrix = 0x50, kFov = 0x40;
    // Triple screen: the camera's field of view widened for all three screens (from its own, kept while ours is in:
    // an update skipped while frozen leaves ours there).
    if (TripleOn())
    {
        static float ownTriple = 0, ourTriple = -1;
        const float now = LoadF(cam + kFov);
        if (now != ourTriple)
            ownTriple = now;
        ourTriple = TripleCameraFov(ownTriple);
        StoreF(cam + kFov, ourTriple);
        ourTriple = LoadF(cam + kFov);
        return;
    }
    if (!VrTracked())
        return;
    static float own[16], posedLast[16], ownFov = 0, posedFov = 0;
    static bool posedValid = false;
    float now[16];
    for (int i = 0; i < 16; i++)
        now[i] = LoadF(cam + kMatrix + i * 4);
    const float fovNow = LoadF(cam + kFov);
    if (posedValid && memcmp(now, posedLast, sizeof(now)) == 0)
        memcpy(now, own, sizeof(now)); // not rebuilt: still ours
    else
        memcpy(own, now, sizeof(now));
    if (!(posedValid && fovNow == posedFov))
        ownFov = fovNow;
    posedValid = false;
    regpu::vr::HeadPose pose;
    if (!FirstPerson(now) || !(FakePose(pose) || regpu::vr::GetHeadPose(pose)))
    {
        for (int i = 0; i < 16; i++)
            StoreF(cam + kMatrix + i * 4, now[i]);
        StoreF(cam + kFov, ownFov);
        regpu::vr::SubmitFramePose(nullptr, ownFov);
        return;
    }
    PoseCamera(now, pose, posedLast);
    const float half = std::max(pose.fovUp, -pose.fovDown);
    posedFov = half > 0.1f ? 2.0f * half + 5.0f * 3.14159265f / 180.0f : ownFov;
    for (int i = 0; i < 16; i++)
        StoreF(cam + kMatrix + i * 4, posedLast[i]);
    StoreF(cam + kFov, posedFov);
    // (Read back: the stored floats are what the next update compares with.)
    for (int i = 0; i < 16; i++)
        posedLast[i] = LoadF(cam + kMatrix + i * 4);
    posedValid = true;
    regpu::vr::SubmitFramePose(&pose, posedFov);
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        Log("vr: in-car head tracking on (camera %08X, fov %.1f deg -> %.1f deg)", cam, ownFov * 57.2958f, posedFov * 57.2958f);
    }
}
