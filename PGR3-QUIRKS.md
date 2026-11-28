# PGR3 quirks catalogue

What we expect to hit, where the expectation comes from, and its status. Addresses are PGR3 base
XEX (before TU5) unless noted. **Never copy PGR4/FM addresses into PGR3: re-find every one.**

Status: `expected` (inferred from another port), `seen` (confirmed in PGR3), `fixed`, `n/a`.

## Recompilation

| # | Quirk | Source | Status |
|---|---|---|---|
| Q1 | Missing PPC/VMX instructions (bdnzt, mtlr/blrl indirect calls, vcmpbfp, vsel128, vctuxs, lvehx/lvxl/stvxl, vsl/vslo/vsro …) | PGR4 `patches/xenonrecomp.patch` | **fixed by using ReXenon** (session 2). XenonRecomp traps on `vupkd3d128` types 2 (2_10_10_10, imm 8) and 6 (4_20_20_20, imm 24) in sub_8225F8A8/sub_8225FA20, and on `blrl` in sub_82491370; ReXenon implements all three |
| Q2 | Divides that trap on zero or overflow on the console's compiler output; must never trap | FM4/FM3/PGR4 | expected |
| Q3 | MSR[EE] toggles (interrupt disable/enable) → global interrupt lock | PGR4 | expected |
| Q4 | Jump tables indexed by the low word; byte-offset tables needing explicit function sizes | PGR4 `switchfix.py`, FM3 (3 functions) | expected |
| Q5 | Leaf functions without `.pdata`, `.pdata` functions split mid-way, functions only data points to | PGR4 (~250 entries) | **fixed** (session 2): 18 gaps of jump-table leaves sized by `tools/pgr3_leaves.py`; a tail-called setter (82264E68); .pdata 82465300 whose epilogue sits after its exception funclet (sized to 82465900) |
| Q5b | 24 functions end in a call that never returns (`ExTerminateThread`, `KeBugCheck`, longjmp, the game's fatal-error/exit wrappers): the check reports them as running off their end | session 2 | fine as is |
| Q5c | 9 pointer targets inside functions, referenced from `.rdata` 821279B0..82127E08: SEH scope-table filters/handlers in the CRT region. Only reached by exception dispatch, which the runtime doesn't do | session 2 | watch: an indirect-call miss there would name one |
| Q6 | Mis-sized thunk swallows the next function (compiles cleanly, fails at runtime) | PGR4 crowd camera flash | expected |
| Q7 | setjmp/longjmp: find by CRT match; XDK 2099's LIBCMT may differ from FM2/PGR4's | FM3/PGR4 | expected |
| Q8 | `mftb`/`__rdtsc` must be a 49.875 MHz guest timebase (raw TSC overflows ticks×1e6 maths) | PGR4 `patch_mftb.py` | expected |
| Q9 | Title Update #5 is a delta (`default.xexp`); recompile the **patched** image | PGR4 (TU2) | seen: **TU5 does not apply to the Europe disc XEX** (image key mismatch; NOTES.md); base XEX targeted for now |
| Q10 | Bink is compiled into the XEX (`BINK` section, 0x1042C bytes of code) and runs as guest code | PGR4 | seen |

## Kernel / XAM (2005 XDK 2.0.2099)

Import coverage (session 2, `out/import_names.txt`): 185 of 265 imports already exist by name in the
PGR4/FM3 runtime, and 22 `Vd*` calls are handled inside ReGPU. The rest:

| # | Quirk | Source | Status |
|---|---|---|---|
| K1 | **Kernel XMA API** (`XMASetInputBuffer0/1`, `…Valid`, `XMASetOutputBufferReadOffset`, `XMABlockWhileInUse`, `XMASetLoopData` …, ordinals 0x22A–0x236). The launch-era XAudio drives the decoder through the kernel, not by poking registers like PGR4/FM | import diff | seen (imports); the runtime's XMA context model needs these entry points |
| K2 | **`StfsCreateDevice` / `StfsControlDevice`**: the game mounts STFS packages itself (DLC via `DLC:\`, maybe saves). Map these to extracted folders | import diff | seen (imports) |
| K3 | `NetDll_*` sockets/QoS/DNS/keys (29 calls): fail cleanly offline (Gotham TV, leaderboards, photo upload, XHV voice) | PGR4 approach | expected |
| K4 | XAM UI/content: `XamContentCreate` (older API than `XamContentCreateEx`), `XamShowAchievementsUI`, `…MarketplaceUI`, `…PlayerReviewUI`, `XamUserCreateAchievementEnumerator`, `XamWriteGamerTile`, `XMsgSystemProcessCall`, `XamGetPrivateEnumStructureFromHandle`, `XGetVideoMode` | import diff | expected |
| K5 | `KeTryToAcquireSpinLockAtRaisedIrql`, `MmQueryStatistics`, `XAudioGetSpeakerConfig` (FM3 has the last one) | import diff | trivial |
| K6 | Variable export `VdGlobalXamDevice` | import diff | expected |
| K7 | FFB library XFFB 2.0.4025 resolves `XInputdFF*` through `XexGetProcedureAddress` | PGR4 `wheel_ff.cpp` | expected |
| K9 | **XEX resources overlap left-over PE sections**: "UIScript" (Lua scripts, 82950000+9EFD3) and the XDBF (829EF000+42409) sit over and between .idata, .XBLD and .reloc. The whole image must be mapped, not section by section | session 2 | **fixed** (src/loader.cpp) |
| K10 | Saves mount through the old `XamContentCreate` (not ...Ex) | session 2 | **fixed** |
| K11 | The race streamer uses ReadFileEx: NtReadFile with an APC routine, completed at an alertable wait | session 2 | fix built, untested |
| K8 | Replays/ghosts written to `GAME:\Game\Replay\…`; must redirect to the user folder (the disc is read-only) | strings | expected |

## Audio

| # | Quirk | Source | Status |
|---|---|---|---|
| A1 | No FMOD: XACT `.xwb` wave banks loaded by the game's own `WaveBank::Load`, played through XAUD (XAudio render driver) | strings, disc | seen |
| A2 | XMA context lock register `0x7FEA1818` must rotate through context IDs or the game resets the hardware | PGR4 `xma_lock.cpp` | **doesn't apply** (Deck session 9: the xma 5 s line counts 0 lock polls in a race; PGR3 uses K1's kernel API) |
| A3 | XMA contexts may hold physical addresses (PGR4) or virtual (FM2) | PGR4/FM2 | unknown |
| A4 | Music: WMA through XMP (`Cannot create XMP source voice`); capture callback wants big-endian interleaved float | PGR4 `xmp.cpp` | **fixed** (session 6): `src/audio/xmp.cpp` (own player: FFmpeg avformat/wmav2/mp3 -> 48 kHz, 256-frame BE float blocks to the game's callback); PGR3 sends its requests three ways: XMsgInProcessCall, **XMsgStartIORequest** (playlists) and **XMsgSystemProcessCall** (play: was an unimplemented import). Countdown "now playing" panel back. `PGR3_NO_XMP`, `PGR3_TRACE_XMP` |

## Rendering

| # | Quirk | Source | Status |
|---|---|---|---|
| R0 | **Races render at 1024x600** (menus 1280x720): the front buffer is 1024x600; the console's scaler upscales it | session 2 | **fixed** (regpu-frame-size, session 5): the guest frame size follows the front buffer; races render at the full output size (1440p: 2560x1440) and the upscalers apply |
| R0b | Buildings black: shader branches after the first exec end (reached by conditional jumps, e.g. ps 576b4098 CF 2 -> 39) were never translated | session 2 | **fixed** (regpu-cf-jump-past-end) |
| R0c | Sky blown out white: the adaptive tone map reacting to the black buildings | session 2 | **fixed** with R0b |
| R1 | Predicated tiling: debug modes `1280x720 2xMSAA 2 Tiles H/V`, `4xMSAA 4 Tiles`; check the real render size against the "1138x640" reports | strings | seen (strings) |
| R2 | Per-tile bytes per pixel with HDR formats (PGR4: bottom third black) | PGR4 | expected |
| R3 | Occlusion queries drive light/sun flares (ZPass vs Total must differ). **PGR3's D3D marks a query's end in the four A counters only** (0xFFFFFEED; B stays 0): ReGPU wanted A and B, so no query ever ended (no answers; triple/stereo replays skipped everything after the first query) | PGR4, FM3; session 7 | **fixed** (ReGPU-V gpu.cpp, D3D12 `regpu-zpd-a-counters-end`); sun glare against the console not yet checked |
| R4 | Fences: the game reuses vertex/index buffers once a fence passes; early fences tear geometry → "truthful fences" + skip Present's block | PGR4 `present_wait.cpp` | expected |
| R5 | Runtime shader compile via the linked D3DX9 HLSL compiler (`GAME:\DontShip\Shaders\`); a static microcode scan may miss shaders | strings | unknown |
| R6 | Resolve aliasing and CPU write-backs (reflections, mirrors, photo mode, garage pictures) | FM3, PGR4 | road strips, car pictures, liveries: fixed (sessions 2-11); **photo mode fixed (session 6)**: 1280x720 8888 resolves in race frames written back (gpu_host.cpp) |
| R7 | Motion blur weighted by alpha; `2_10_10_10` front buffer | PGR4 | expected |
| R8 | **The scene is drawn from the race camera object** (0x828B3840: matrix +0x50, FOV +0x40), not from the view hand-off (sub_82157EF8), whose matrices only reach culling, LOD and motion blur; the projection is reversed and infinite (z = 0.05, depth = near / w); the HUD is drawn over the tone-map composite (a7484f3b4c5ba1e1) | session 7 | used by triple screen and VR (src/hooks/vr_camera.cpp) |

## Game logic and timing

| # | Quirk | Source | Status |
|---|---|---|---|
| G1 | 30 fps render with a fixed-rate physics thread; above 30/60 fps one-frame inputs get lost (gear presses), menu repeat counted in frames, walking quantised | PGR4 `gear_latch`, `garage_walk` | session 3: the 30 fps lock is removed (vblank 2x the cap); game time follows its clock (race start unchanged at every rate). Frame-rate-tied input/menu bugs **not checked yet** at high fps |
| G2 | Game bugs exposed by recompilation: shadow frustum byte cleared too early; grid lookup loops forever at huge positions | PGR4 `car_shadow`, `grid_bounds` | maybe |
| G3 | Lua **5.0.2** UI (not 5.1). luaD_throw 8233D9D0; lua_dofile 8234B940 (luaL_loadfile 8234B778, errors reported only via `_ALERT`); scripts are looked up by name in the BZUI pack table (8231D8E0) | session 2 | logged by src/hooks/lua.cpp |
| G4 | Intro skip: fake the Bink "movie finished" result | PGR4 `skip_intro.cpp` | expected |
| G5 | PAL movies (`*_25.bik`) on the Europe disc: the game may choose 50 Hz content by video mode | disc | unknown |
| G6 | Game clock 0x82545774 (ticked by sub_82158BC8 in the world update sub_82299690); presents every second vblank; its own one-frame throttle in sub_821507A0 (option byte 0x825445D8). The countdown (GO = Lua RaceStarted) runs on another clock. Session 4: the swap's GPU-side wait is `WAIT_REG_MEM A140E004 == 0` (twice a frame), a flag D3D's vblank handler sub_823CFF78 clears (found with `PGR3_WATCHW=A140E007`); the next frame waits on `A140E000 == 4`, set by a store earlier in the stream | sessions 3-4 | frame_time.cpp freeze, frame_wait.cpp switch; uncapped: ReGPU `REGPU_VBLANK_ON_WAIT` (gpu_host.cpp) |
