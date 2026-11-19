# PGR3 ReGPU: notes

## Start here (session 4)

**State (end of session 3, 2026-11-19):** boot to race works and the London grid matches the console. The 30 fps lock
is removed and the first optimisation pass is in: AI benchmark race, 720p uncapped, **29.7 -> 296 fps** (FM3's defaults
unlocked: 232). Repo: https://github.com/braaap-stututu/PGR3-ReGPU-recomp (public, main). ReGPU patch stack: 89.
The user's last request was to keep optimising: do the measurable work, park anything that needs their opinion, and
report when they ask.

**Open decisions for the user (don't change without asking):**
1. `REGPU_INDEX_SAMPLED_HASH` on? +1.5% (4/4), not exact.
2. `REGPU_NO_WAIT_DRAIN` + `REGPU_NO_INTERRUPT_DRAIN` on? +1.5% together, ordering not proven safe.
3. Upload ring in system memory as PGR3's default (FM3: VRAM, the user's choice there)? +9.7% here.
4. Default frame cap: still max_fps 60 + vsync; raise it?

**How to measure (session 3 tools; Claude may run these itself, minimized, on out\user-route, never out\user):**
- `. .\tools\ab.ps1; Start-Pgr3AbGrid` (uncapped frozen grid, ~30 s to freeze), then `Invoke-Pgr3AB REGPU_X`
  (live switch, interleaved rounds; `-Off 1 -On 0` to test turning a default off; `-Env` for PGR3_* switches),
  `Measure-Pgr3Fps`. Boot-time options: `Invoke-Pgr3BootAB ([ordered]@{ a = @{}; b = @{ NAME = 'v' } })`.
  Grid at final defaults: ~250 fps (720p 1x, vblank 2000).
- `. .\tools\benchmark.ps1; Invoke-Pgr3Benchmark -Label x -Seconds 60 -Env @{ PGR3_MAX_FPS='360'; PGR3_VSYNC='0' }`:
  the AI drives the race, results in out\bench\runs.csv. Final defaults: 295.2 / 296.3 fps, p99 9.4 ms.
- Profiling: `PGR3_SAMPLE_THREAD=name:gpu draw worker` (or a guest thread id: 1 = game main, 5 = command processor),
  `PGR3_SAMPLE_CALLERS=6`; live `switch REGPU_PERF 1` for CP wait targets and drain counts.
- Close the test game before every rebuild (`Close-Pgr3`): a running exe makes the link fail and the old exe runs.
- New ReGPU patch: edit thirdparty/ReGPU, then `.\tools\make_patch.ps1 -Name regpu-x`, add it to tools/setup.ps1 and
  patches/README.md, `.\tools\setup.ps1 -NoBuild` (must say "all N patches already applied").
- PowerShell 5.1 `Set-Content -Encoding utf8` adds a BOM: strip it (`sed -i '1s/^\xEF\xBB\xBF//'`) or use Edit.

**Where the frame goes (frozen grid, 4.2 ms):** the game's main thread spends ~55% spinning in D3D's block-on-fence (a
real dependency: the driver-animation vertex-buffer lock, after its own one-frame throttle); ReGPU's command processor
waits 73% in `work::Drain` (~8 drains a frame); the draw thread works ~78% (DrawImpl flat: content/index hashing,
pipelines, WindowScissor, uploads). The GPU (RX 7900 XTX) needs 2-3 ms at any resolution. So the limit is ReGPU's draw
thread and the CP/draw-thread handoffs, not the game's own code.

**Next optimisation ideas (not tried yet), roughly by expected value:**
1. Draw-thread per-draw cost: line profile (`PGR3_SAMPLE_LINES=1`) of DrawImpl and its lambda_63 (4.8%), GetIndexBuffer
   + FullHashAvx2 (~5%), ContentHash (3.7%), WindowScissor (1.9%); look for PGR3-specific repeats (4,300 draws: ~940 with
   ps 8c8b5358 (depth/shadow pass?), ~1,060 with the building ps 576b4098).
2. Why pieces/workers don't raise fps (draw thread 52% busy with 6 workers, same fps): find the next limit in that mode
   (CP drains? replay? fence latency) with the sampler on each thread.
3. Fewer drains: which WAIT_REG_MEM polls (mem 0x0140E006, fn 3) need the draw thread at all; a targeted drain instead
   of the switch-wide one in patch `regpu-drain-switches`.
4. Uncapped vblank: 3000 Hz gave +1% over 2000 on the grid; its CPU cost is not measured.
5. Higher resolutions/MSAA and a weaker-GPU check (the upload-ring trade-off flips when GPU-bound).
6. FM3 ideas that don't apply while the game waits on ReGPU: ReXenon register opts (X1), PGO (X10); revisit only if
   the game's main thread becomes the limit.

**Other open items:** frame-rate-tied bugs at high fps (PGR3-QUIRKS G1: gear presses, menu repeat; not checked);
race frame scaling (1024x600 presented centred: stretch it like the console's scaler); rest of the
race vs the reference video (reflections, shadows, motion blur, results screen); unimplemented imports
(`MmQueryStatistics`, `XNotifyPositionUI`, `Refresh`, `XMsgSystemProcessCall`, `XamUserCreateAchievementEnumerator`, a few
`Vd*`); intro skip, DLC, saves/profiles; send `regpu-cf-jump-past-end` upstream.

Testing: the user runs `play.cmd` (out\user). Claude: `. tools\pgr3ctl.ps1; Start-Pgr3Dev` (port 7342, minimized,
out\user-route); the grid route (`Start-Pgr3Grid`) replaces the old manual pad route. Permission to open/close the game
was given for session 3's few hours only: ask again. Reference video: YouTube 5_n5CbsLcrI (0:00-7:50).

Plan: [PLAN.md](PLAN.md). Quirks: [PGR3-QUIRKS.md](PGR3-QUIRKS.md). Rules: [CLAUDE.md](CLAUDE.md).

## TU5 mismatch (session 2)

`xexpatch` (XenonUtils' XexPatcher) fails with `PatchIncompatible`. The patch's header offsets and sizes fit
(target headers 0x3000 = base header size 0x3000), so the failure is the **image key check**: the key
the xexp carries for its source does not match the Europe disc XEX's key. TU5's delta descriptor:
target version 0x504, source version 0, source digest `1885722d…003a`. The base XEX is version 0xB,
media ID 1E371802. The TU was most likely made for a different disc release (another region or
revision).

**Most likely the disc already contains the updates.** The disc XEX's PE timestamp is **2006-10-05**,
almost a year after the Nov 2005 launch. Its XFFB library is XDK 2.0.4025 while the rest is 2099
(relinked later). It already has DLC code (`DLC:\` paths, `DriverSetupDLC%d.lua`, `dlcui_Resource.xpr`),
and its version is 0xB, while TU5's delta says source version **0** (the launch XEX). So this Europe image
is a later pressing that rebuilt the game with the patches folded in, and TU5 is the patch for the
original 2005 disc. This can't be confirmed byte-for-byte without the 2005 XEX, but it is the
consistent reading. Decision: **target the disc XEX (`-Base`)**; TU5 is not needed. Every address is
from the disc XEX. (Checked in session 2 after the user suggested it.)

## Game files (user-supplied, never commit)

| File | What | Notes |
|---|---|---|
| `Project Gotham Racing 3 (Europe).iso` | disc image, XGD2 | 849 files, 6.37 GB |
| `game/disc/` | extracted disc | extracted with our own XDVDFS reader |
| `game/default.xex` | base XEX | SHA-256 `26fb114109f0fa997497c6836385b8af391db253d75c08d78d12f4899475a78a` |
| `game/default.xexp`, `game/gw.xexp` | **Title Update #5** (from `75528D49/TU_16L61UH_…184`, STFS LIVE, content type 0xB0000) | delta patches; base must be patched before recompiling, like PGR4's TU2 |
| `4D5307D1/00000002/*` | DLC Packs 1–3 (STFS LIVE, content type 2, Marketplace) | Pack 1: 13 cars, Pack 2: 3 cars, Pack 3: 15 cars. Each pack holds `.dat/.pak_hrd/.xwb` per car, `cardata.pak`, `drivers.pak`, `PGR3.ipx`, `dlcui_resource.xpr`, `*DLC.ini` per language. The game reads them via `DLC:\` |

## XEX facts

- Title ID **4D5307D1**, media ID 1E371802, version 0xB, flags: title (XEX2 flags 1).
- Retail encrypted (1), basic compression (1). FM3's `xexdump.exe` (XenonUtils loader) decrypts it fine → `out/pgr3_base.bin`.
- Image base 0x82000000, size 0xA40000, entry **0x823C7BB0**, default stack 256 KB.
- **XDK 2.0.2099** (launch-era; FM3/PGR4 are much later). Static libs: XRTLLIB, XAPILIB, D3D9 (16390), D3DX9, XBOXKRNL, XAUD, LIBCMT, XGRAPHC, XONLINE, XHV, XMP, **XFFB 2.0.4025** (newer than the rest).
- Sections: `.rdata` 82000400, `.pdata` 82128000 (0x15C88), `BINKBSS`, `.text` 82150000 (**0x3A0C78**, 3.6 MB), `BINK` 824F0E00 (code: Bink is compiled into the XEX, as in PGR4), `.data` 82510000 (4.2 MB), `BINKDATA`, `.tls`, `.XEXID`, `.idata`, `.XBLD`, `.reloc`.
- Imports (`out/imports.txt`): **xam 100 functions; xboxkrnl 155 functions + 10 variables** (265 total; PGR4 has 293). Ordinals only so far; names still to map.
- Also on disc: `gw.xex` (Geometry Wars: Retro Evolved, launched from `GAME:\gw.xex`). Out of scope, as in PGR4.

## Engine findings from the image and disc

- **UI is Lua 5.0.2** (`$Lua: Lua 5.0.2 …`), not 5.1 like PGR4. About 300 `.lua` script names are in the XEX (`Garage\GarageMenu.lua`, `GothamTv\MainMenu.lua` …), so the scripts are built into the binary or a pack. PGR4's `hooks/lua.cpp` cannot be reused as-is; the structures differ between 5.0 and 5.1.
- **Audio**: XACT wave banks (`.xwb`, 119 on disc) read by the game's **own** `WaveBank::Load` (rejects streaming banks), played through XAUD (XAudio). **No FMOD** (PGR4 uses FMOD). Music: `.wma` (202 files) through **XMP** source/submix voices, like PGR4's XMP path. Plus 7 `.mp3` files.
- **Rendering**: debug strings name the tiling modes: `1280x720 2xMSAA 2 Tiles Horizontal/Vertical`, `1280x720 4xMSAA 4 Tiles Horizontal`, `640x480 4xMSAA 2 Tiles Vertical Split`, plus `EDRAM Used` / `Tile Count` / `Tilesize` debug readouts. So predicated tiling with 2× MSAA at 1280x720 is likely. **Check this against the "renders at 1138x640" reports at first frame.**
- D3DX9's HLSL compiler is linked (`FETCHMULTISAMPLE`, `CompileWithMSAA…`) and there is a `GAME:\DontShip\Shaders\` path. The game may compile some shaders at runtime. If so, PGR4's static XEX shader scan will miss those; the pipeline seed has to cover them.
- Data: `.stream2` (18 files, 3.7 GB, track streaming), `.pak`/`.pak_hrd` (cars, areas), `.gm3`, `.pk2` (crowds), `.xpr` (XDK packed resources, not PGR4's `.bpr`), `.ipx`, `.dat`, Bink `.bik` movies (intro and a `_25` 50 Hz-PAL variant), sky `.tga`.
- Online: Gotham TV client (`E:\xetv\…\Ticker.cpp`), leaderboards, photo upload, XHV voice. All to be stubbed offline.
- Replays and ghosts: `GAME:\Game\Replay\…`. These write to the game: device, so the VFS must redirect them to a writable user folder.
- Physics/driver tuning is exposed as a large debug-variable tree (`Dynamics|…`, `Driver|Physics|…`). Useful later for finding the physics step.

## Tools written (in `tools/`)

- `xiso.py`: XDVDFS list/extract.
- `stfs.py`: STFS list/extract (works on the TU and DLC packages).
- `xexhdr.py`: XEX2 optional-header dump.
- `imports_raw.py`: first draft of the import lister; the working version is a one-off whose output is `out/imports.txt` (to redo properly in Phase 3).

## Session log

### Session 1 (2026-11-19)
- Researched the FM3 and PGR4 ports and wrote PLAN.md.
- Inventoried the user's files, extracted the disc and the TU, decrypted the base XEX and collected the findings above.
- Not done: names for the import ordinals; applying TU5 to the XEX; checking the render resolution.

### Session 2 (2026-11-19)
- Imports named (`tools/import_names.py` → `out/import_names.txt`). 185/265 exist in the PGR4+FM3 runtime,
  22 `Vd*` are ReGPU's, and the rest are listed in PGR3-QUIRKS.md K1–K6. The important gaps are the kernel
  **XMA API** (0x22A–0x236) and **`StfsCreateDevice`/`StfsControlDevice`**.
- Cloned current upstream PGR4-ReGPU (99 commits, HEAD 6d777f7) into `reference/PGR4-ReGPU` (read-only).
  The user's own clone in `PGR4 ffb research` was left untouched.
- Wrote PGR3-QUIRKS.md and CLAUDE.md.
- Skeleton: `git init`; `.gitattributes` from FM3; `.gitignore` covers game data, ISO, TU and DLC folders.
  Submodules pinned to FM3's commits: XenonRecomp ddd128b, ReGPU 57143cb, ReXenon 171c866,
  meshoptimizer f0a684f.
  - Gotcha: the first clone came out CRLF (global autocrlf), so setup refused it. Fixed by setting
    autocrlf=false per submodule and re-checking out; the pinned gitlinks are now staged.
- `tools/setup.ps1` (FM3's, unchanged stacks): all 89 patches applied (5 XenonRecomp, 83 ReGPU,
  1 ReXenon). Host tools built into `build/tools` and `build/tools-rexenon`.
- New host tool `xexpatch` (XenonUtils' XexPatcher). New `tools/recomp.ps1` (SHA checks, TU,
  xexdump, `-Prepare` XenonAnalyse, `-Base`).
- `recomp.ps1 -Prepare -Base`: XenonAnalyse found **252 jump tables** (`config/pgr3_switch_tables.toml`,
  base XEX). TU5 failed to apply (above).
- Nothing committed yet.
- **Phase 2 (same session):**
  - `tools/pgr3_config.py` (ported from FM3). It found all 8 save/restore helpers (one match each) and
    3 exception handler words (227/49/1 functions). 11153 `.pdata` functions.
  - setjmp 8247F3D0 (no .pdata) and longjmp 8247F2B0, confirmed in ppcdis (FM3/FM4 CRT layout).
  - First XenonRecomp pass: 17187 functions, 115 unresolved branches, 5 traps, 11 run-offs. Almost all
    of the 115 came from **early-XDK leaf functions without .pdata that hold jump tables**: the sweep cut
    them at the bctr. New `tools/pgr3_leaves.py` sizes every function in those gaps from real starts
    (bl/pointer targets, ignoring jump-table addresses and inline tables) → 115 down to 2.
  - The last two were fixed by hand: the setter 82264E68 reached by a tail-call; sub_82465300's epilogue
    placed after its exception funclet.
  - The XenonRecomp traps (`vupkd3d128` types 2 and 6, `blrl`) are all implemented in ReXenon.
    **ReXenon result: 16630 functions, 0 unresolved, 0 traps**; 231 functions were extended over
    fragments by ReXenon itself.
  - The 24 run-offs all end in a call that never returns (ExTerminateThread, KeBugCheck, longjmp, the
    game's fatal-error/exit wrappers). The 9 pointer targets are SEH scope-table funclets (QUIRKS Q5c).
  - `CMakeLists.txt` (PPC-only for now) + `src/ppc_hooks.h`, `tools/patch_mftb.py`, `tools/genstubs.py`
    from FM3. `ninja -C build/ppc pgr3_ppc` compiles all generated code with no errors (`pgr3_ppc.lib`, 43 MB).
  - ReXenon notes that the indirect-call table at 0x82A40000 is too far for a 32-bit displacement and
    suggests a `function_table_address` (perf option for later).
  - New tools: `pgr3_config.py`, `pgr3_check.py`, `pgr3_leaves.py`, `pgr3_gaps.py`, `pgr3_errors.py`,
    `import_names.py`.
- **ReXenon only (user decision, end of session 2):** hedge-dev XenonRecomp is gone. The submodule and FM3's 5
  xenonrecomp patches were removed. The host tools (`xexdump`, `xexpatch`, `ppcdis`) build on ReXenon's
  XenonUtils, and ReXenon's XenonRecomp/XenonAnalyse build into `build/tools/rexenon/`. `recomp.ps1` has no
  `-Recompiler` switch and writes `out/ppc`; CMake has no `PGR3_RECOMPILER`; `patch_mftb.py` was removed.
  - ReXenon's XenonAnalyse finds 251 tables, not 252. It misses sub_821B12D0's table at 821B143C (r10,
    29 cases), which the FM4-patched analyser found by back-tracing. That table is now kept by hand in
    `config/pgr3_switch_tables_extra.toml`, which `-Prepare` appends.
  - Result unchanged: 16630 functions, 0 unresolved, 0 traps; `pgr3_ppc.lib` compiles cleanly.
- **Phase 3 (same session), runtime and first boot:**
  - The runtime is **FM3's `src/`**, not PGR4's. FM3's builds against exactly our ReGPU (57143cb + 83 patches)
    and ReXenon, while PGR4 vendors a different ReGPU.
  - All FM3 game hooks were removed. PGR3 has no XAPI fibers (no CreateFiber code in the image).
  - `FM3_*` switches were renamed `PGR3_*`. The window title is "Project Gotham Racing 3".
    `src/hooks/pgr3_placeholders.cpp` stubs FrameTimeSetPacing / GridTestPressA / OneTileEnabled.
  - `thirdparty/{ffmpeg,fidelityfx,aa}` are junctions to the FM3 project's copies (gitignored).
  - CMake builds `pgr3.exe` (ReGPU, FFmpeg, d3dcompiler/dxc copied). Added `play.cmd` and `tools/pgr3ctl.ps1`.
  - Kernel XMA API: added `XMASetInputBuffer0/1`, `XMASetLoopData` and `XMABlockWhileInUse` (src/audio/xma.cpp).
    The rest were already in FM3's runtime (some are generated by macros, which the earlier name search missed).
  - First boot reached the intro movies and the disclaimer screen. Leaving them crashed with `KeBugCheck`
    from `exit()`, after an **unprotected Lua error** ("attempt to call a nil value": global `UISetup` missing).
    `src/hooks/lua.cpp` now logs every error that `luaD_throw` (8233D9D0) sees.
  - Cause: the UI scripts are an **XEX resource** "UIScript" (a "BZUI" pack, 285 scripts, 82950000+9EFD3). It
    overlaps the left-over PE sections .idata/.XBLD/.reloc and fills the gaps between them. The loader copied the
    image section by section, so the gaps stayed zero. **Fix: `src/loader.cpp` copies the whole image**, as the
    console maps it. After that the game reaches the title screen and the main menu.
  - Unimplemented imports seen so far: MmQueryStatistics, VdGetGraphicsAsicID, VdInitializeEDRAM,
    XNotifyPositionUI, Refresh, XMsgSystemProcessCall, XamContentCreate. Audio not checked yet.
- **Garage and race load (end of session 2):**
  - The user's run hung going to the garage. The game mounts its save device with the **older `XamContentCreate`**
    (overlapped in r9), which only had a stub returning success. `SAVE:` was never mapped, so `SAVE:\profile.save`
    failed and the game waited. Fix: `XamContentCreate` and `XamContentCreateEx` share `ContentCreate()`
    (imports_xam.cpp). Checked in a scripted run: profile.sav is created, then Career menu, championship select,
    and the garage ("View Your Cars", Honda NSX GT2) all render.
  - The next stall is "Use this car" (race load): the London streamer reads `London_sunny.stream2` with **ReadFileEx**
    (NtReadFile with an APC routine), which the runtime ignored, so its completion routine never ran and the load
    waited forever (fps 0). Fix: user APCs. `QueueUserApc` / `DeliverUserApcs` (thread.cpp, a per-thread queue);
    NtReadFile/NtWriteFile queue `apcRoutine(apcContext, iosb, 0)`; the alertable waits (KeDelayExecutionThread,
    KeWaitForSingle/MultipleObject(s), NtWaitForSingle/MultipleObject(s)Ex) run queued APCs and return
    STATUS_USER_APC. Built, **not yet tested** (needs the user's run into a race).
  - User's race-load test: the APC fix let the load go further, then it hung with no frames. ReGPU's hang report:
    "waiting 20 s on shader: compile jobs 1 (queued 1, background 0, parked 14)", every compile worker asleep in
    `cv.wait`, and the game spinning in D3D's GPU-hang check (sub_823DEE80). Cause: **ReGPU bug**:
    `ShaderCompiler::Promote` moved a parked shader into the urgent queue without `cv.notify_one()`.
    New patch **`regpu-promote-notify`** (84th in the stack, listed in setup.ps1 and patches/README.md, stack check
    OK). It affects FM3 too; worth sending upstream. Not rebuilt or tested yet (the user's game was still open).
  - Next user test: the race loads and runs (30 fps, 3000-4300 draws a frame) but the window showed the loading screen.
    A capture showed **races render at 1024x600** and resolve a 1024x600 front buffer at 01411000; the swap said
    "not resolved: showing the render target", because ReGPU presented a resolved front buffer only when it was
    exactly 1280x720. New patch **`regpu-front-buffer-size`** (85th): any R8G8B8A8 front buffer up to the output size
    is presented, a smaller one centred (letterbox) for now. Proper scaling, like the console's display scaler, is
    a follow-up. `REGPU_FRONT_BUFFER_EXACT=1` restores the old behaviour. play.cmd now sets `REGPU_DEBUG_PORT=7342`.
  - The race now shows (user: "loaded correctly, lots of broken graphics"). A debug shot during the race crashed the game: the shot's readback copied 720 rows of the 600-row front buffer. Fixed in the same patch (shot at the image's own height, null-map guard).
- **Rendering parity, first look (race, London, paused):** 4353 draws, 0 skipped, no shader compile failures.
  Road, cars, crowd and HUD look right. Broken:
  1. **Buildings are solid black**: all of them are pixel shader `576b4098a45814c0` (1059 draws; found by tinting).
     Its output colour really is 0 (`ps ... float4(oC0.xyz,1)` = black). It ends `oC0.xyz = r6.xyz * pc[7].y`, and
     pc[7].y is non-zero (about 0.34), so `r6` is 0. Next: walk `r6` back through its fetches (t0, t3, t7 cube?,
     t11) and the log2/exp2 block (lines ~640-690 of `out/run/out/hlsl/576b4098a45814c0.hlsl`). Suspects: a texture
     with the wrong format, or a resolved lightmap/shadow map read as 0.
  2. **Sky blown out to white**; the full-screen pass `6be16ccf5f4b5c25` (post-process / tone map) is the place to
     start (HDR exposure from a luminance downsample is the usual cause).
  - Capture of that frame: `out/capture_race.txt`. Shots: `out/shots/race_a.png`, `tint_*.png`.

## Reference: console boot to first race

User-supplied video (YouTube 5_n5CbsLcrI, "PROJECT GOTHAM RACING 3 | Xbox 360 Gameplay", 480p capture; boot to the
first race covers 0:00-7:50). Frames checked in session 2:
- 0:08 Microsoft Game Studios logo (white/green). 0:20 disclaimer: red with the multilingual warning text. Ours
  shows the same text on a red banner with the 3D street scene above and below; check whether that is the same
  screen at another moment of its camera move.
- 0:45-2:20 intro movie (London, Tokyo at night, Nürburgring). Ours plays the intro Bink too.
- ~2:55 car purchase screen (Jaguar XKR, 85,000 CR, car stats, Gotham Sport comment). ~3:30 garage map
  ("Starter Garage", overhead plan of parking spaces).
- 4:10-7:10 first race, London in daylight (Skyline GT-R): **buildings are pale stone with windows and textures,
  trees along the road, blue sky with white clouds**, HUD (position, target, lap, gear/speed bottom right, map
  bottom left). Compared with ours: the black buildings and white sky are both wrong; road, cars and crowd match.
- 7:20 race results ("TARGET 3rd").

### Session 2 (cont.): black buildings traced to an empty lighting table
- Building pixel shader `576b4098a45814c0`. Its final colour is about 0.016, about 25x too dark (measured live).
  Working back from it:
  - Vertex colour `o[6] = (0,0,0,1)` in both building vertex shaders (2637a999 / 1642ebaa): the colour fetch at
    offset 0 with swizzle 0001. That's what D3D on the 360 does for an element the vertex declaration lacks, so it is
    probably genuine.
  - Lightmap t3 (DXT1 2048x2048 @02465000): decoded correctly (checked by hand from guest memory). Green is 0
    across the atlas, which is probably genuine too; blue indexes t11.
  - **t11 (8888 16x16, linear, @0ED2D000) is all zero in guest memory** except its first ~40 bytes. The walls' main
    light is `diffuse * t11.a * pc[15].w`, so zero means black walls. No resolve and no memexport draw writes it in a
    race frame, so it is written at load (by the CPU, or by a memexport shader, whose writes ReGPU drops).
- **White sky:** the sky shader `f74d3ed79a32dfd2` outputs about (0.8, 1.0, 1.2), which is sane. The tone map
  (`6be16ccf`) adapts to the frame's average brightness, which the black buildings drag down, so it is likely a
  side effect.
- Next step: run `play_trace.cmd` (play.cmd + `PGR3_WATCHW=AED2D040`) into a race. The log then shows either
  `watchpoint: thread N wrote ...` with the guest stack (CPU writer), or `memexport: vertex shader ... drawn`
  (new patch `regpu-memexport-log`) at load.
- Tools: `Probe-Pgr3` in tools/pgr3ctl.ps1 (live shader probes read back as pixel values). Registers in
  translated shaders are `r0..r9` (not `r[n]`); an edit that doesn't compile drops the draw, which shows whatever is
  behind it.

### Session 2 (cont.): black buildings and white sky FIXED
- (The user allowed Claude to close and start the game itself for a couple of hours on 2026-11-19, so the race
  route was driven with the virtual pad from `out\user-route`.)
- The `t11` lighting-table lead was a red herring. A watchpoint over all three physical views saw no write during
  the race load, and no memexport draw happened. The table sits in a 228-byte allocation that Bink's menu buffers
  used before. The draws that read it light correctly.
- Real cause: **ReGPU stopped parsing the control-flow program at the first exec end** and read on only for
  *calls* past it. PGR3's compiler places whole branches after the end, reached by conditional *jumps*. Building
  ps 576b4098 jumps from CF 2 to CF 39 when bool 130 is set (the right-hand buildings, County Hall). The translation
  had no blocks 31+, so those pixels came out 0, and the adaptive tone map then pushed the sky to white.
- Fix: new ReGPU patch **`regpu-cf-jump-past-end`** (87th). Jump/loop targets past the end are parsed, and parsing
  continues through a reached branch until its own end, return or unconditional jump. The vertex-fetch-layout scan
  translates such programs in full. `REGPU_NO_JUMP_PAST_END=1` restores the old behaviour. Affects any title; worth
  sending upstream.
- Result (`out/shots/fix2_race.png`): London grid matches the console reference (pale stone buildings with windows,
  blue sky with clouds, lampposts/railings, gantry, Big Ben). The garage is better too (posters, neon, a lit car).
- Also added: `PGR3_WATCH_RESET` (live; re-arms the watchpoint's 40 logged hits), `play_trace.cmd`, `Probe-Pgr3`,
  and the `regpu-memexport-log` diagnostic patch (no memexport seen so far).
- Still to do in rendering: scale the 1024x600 race frame to the window (now centred with black bars), then go
  through the rest of the race against the reference video (reflections, shadows, motion blur, results screen).

### Session 3 (2026-11-19): GitHub repo, test grid and AI benchmark
- Repo created: https://github.com/braaap-stututu/PGR3-ReGPU-recomp (public, user's choice), commit 9ca2145 on main.
  Commit identity set per repo (same as FM3's).
- **AI drives the player's car**: the car-build function sub_82178CB8(car, entry) copies the car's type from entry+68
  (0 player, 1 opponent, 4 the disclaimer screen's display car). Building the player's car as type 1
  (`PGR3_AI_PLAYER=1`, src/hooks/ai_player.cpp) makes the race AI drive it while the HUD, chase camera and position
  follow it (1st at 119 mph on lap 1). Found from its debug tweakables ("Game|Cars|Car%d|Player/AI" etc.).
- **Route** (src/hooks/grid_test.cpp, `PGR3_GRID_TEST=1` / `PGR3_BENCHMARK=N`): A and START in turn from boot. With
  out\user-route's profile that reaches a London race in ~13 s; the presses stop at the race's car build (type 4
  ignored). `GridTestPressA` became `GridTestButtons` (imports_xam.cpp ORs the mask in).
- **Freeze** (src/hooks/frame_time.cpp, `PGR3_FREEZE_TIME`, live): the game clock 0x82545774, ticked by
  sub_82158BC8 inside the world update sub_82299690, is held after each tick (dt 0, start offset moved on), so the
  world stops while frames are drawn and resumes without a jump. Main loop chain found with the watchdog's deeper
  stacks (`PGR3_WATCHDOG_DEPTH=N`, new): main 822A07F8 -> loop 821B5B88 -> app vtable +8 -> 8229B800(game).
- **GO**: Lua RaceStarted (sub_823595F8) at 5.16 game seconds after the race's first frame. The countdown runs on
  another clock, so GO still fires while frozen. Freeze default 4.6 s (`PGR3_GRID_FREEZE_AT`).
- New tools: tools/benchmark.ps1 (Invoke-Pgr3Benchmark, Start-Pgr3Grid, profile restore from out\bench\baseline),
  tools/bench_summary.py (our own perf lite summary). Addresses in src/hooks/addresses.h.
- Two 60 s runs: 29.7 / 29.8 fps, p99 35.1 ms, GPU 2.3 ms, worst 601 / 419 ms (first-time pipeline builds).
- Not done: the DemoMode race-setup field (setup+2152) was found but not needed; the route's race depends on the
  route profile's career state (kept fixed by the baseline restore).

### Session 3 (cont.): 30 fps cap removed, first optimisation pass (autonomous, user's request)
**Frame rate:** PGR3 presents every second vblank and keeps game time by its clock, so (as PGR4-ReGPU does) the guest
vblank now runs at twice the frame cap (src/gpu_host.cpp). Uncapped (max_fps 360) it runs at 2000 Hz, since frames wait
for a vblank boundary when they present (ReGPU patch `regpu-vblank-hz-4000` lifts the 1000 Hz clamp). Game speed checked:
GO still 5.1 game seconds after the race's first frame at every rate. Default play settings unchanged (max_fps 60, vsync
on -> vblank 120 Hz, 60 fps).

**Results, AI benchmark race (720p 1x, uncapped, 60 s, 2 interleaved runs each, RX 7900 XTX, 16 threads):**
| Step | fps |
|---|---|
| Console lock (vblank 60) | 29.7 / 29.8 |
| Unlocked, old (FM3) defaults, vblank 720 | 230.7 / 233.2 |
| + sampled uploads + upload ring in system memory | 261.2 / 263.9 |
| + vblank 1000 Hz | 277.9 / 277.8 |
| + vblank 2000 Hz + constants by count (final defaults) | 295.2 / 296.3 (**+27.5%** over the old defaults) |

**Adopted (src/gpu_host.cpp):**
- `REGPU_UPLOAD_SAMPLED` (PGR4 ships it): grid +7.9% (4/4). Not exact in principle (a changed buffer the 16 samples miss
  shows old data for a frame; large changing ranges are still hashed in full). Picture same on the grid; a 50 s AI drive
  looked right (smoke, cars, crowds). `PGR3_FULL_UPLOAD_HASH=1` off.
- Upload ring in system memory (`vram_upload_ring=1` for VRAM): grid +9.7% (4/4) at 720p, +3.1% at 1440p 4x. FM3 has
  it on by the user's choice; PGR3 is draw-thread bound at every resolution here (GPU 2.4-3.0 ms of a 4.2 ms frame).
- `REGPU_CONSTANTS_BY_COUNT`: +2.1% (3/3), exact (copies more constants, never fewer). `PGR3_MASKED_CONSTANTS=1` off.

**Measured and kept as they were (frozen grid, live, 3-4 interleaved rounds; "off" = turning a default off):**
async present off -4.6%; texture-check stagger off -1.8%; direct dynamic indices off -12.2%; vertex arena off -7.8%;
lazy loop constants off -2.6%; draw workers off / fence order on / sync splits / dynamic expire / direct dynamic /
index hash once / index check once / stable upload reuse / A2M as test / aniso from fetch / no shader scan / no texture
threads / split alpha test off / 1010102 copies off: all within +-0.5%; constant reuse on -7.3%; texture check every 4
-2.1%; fetch specialise off: GPU 2.96 -> 3.66 ms (keep). Boot A/Bs: submit every 512 draws -1.6%, draw-thread pieces
(2/3/6 workers) -0.6..-2.6% (the draw thread's busy time drops to 52% but fps doesn't rise), no draw thread -10.8%.

**Where the time goes (frozen grid, sampler `PGR3_SAMPLE_THREAD`):** the game's main thread spends ~55% spinning in
D3D's block-on-fence (sub_823D0B48/823DEE80), first in its own one-frame throttle (sub_821507A0 -> sub_823D0D50 at
0x821508DC, option byte 0x825445D8), and with that skipped (`PGR3_NO_FRAME_WAIT=1`, src/hooks/frame_wait.cpp, live,
no gain: -0.5%) in the car-driver animation's double-buffered vertex-buffer lock ("update drivers", sub_8226EA20) instead,
a real dependency. ReGPU's command processor waits 73% in `work::Drain` (about 8 drains a frame: memory-polling
WAIT_REG_MEM and INTERRUPT); the draw thread works ~78% (DrawImpl flat: hashing, index buffers, pipelines). The game waits
on ReGPU, so the game's own CPU code (ReXenon/PGO, FM3's X1/X10) is not the limit.

**Need the user's opinion (left off):**
1. `REGPU_INDEX_SAMPLED_HASH`: +1.5% (4/4), not exact (FM3 X15 found the same ceiling).
2. `REGPU_NO_WAIT_DRAIN` + `REGPU_NO_INTERRUPT_DRAIN` (new patch `regpu-drain-switches`): +1.5% together; the ordering
   against the draw thread's writes isn't proven safe.
3. Upload ring default now off for PGR3 (FM3's default is on by the user's choice).
4. Default frame cap: still max_fps 60 + vsync for normal play; uncapped needs max_fps=360, vsync=0.

**New tools:** tools/ab.ps1 (`Start-Pgr3AbGrid`, `Invoke-Pgr3AB` live switch A/B, `Invoke-Pgr3BootAB` boot A/B,
`Measure-Pgr3Fps`), tools/make_patch.ps1 (new ReGPU patch from the working tree over the stack). ReGPU stack: 89.
