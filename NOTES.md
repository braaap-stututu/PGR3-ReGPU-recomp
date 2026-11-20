# PGR3 ReGPU: notes

## Start here (session 6)

**State (end of session 5, 2026-11-20):** boot to race works, the London grid matches the console. Repo
https://github.com/braaap-stututu/PGR3-ReGPU-recomp (public, main; session 5 committed and pushed: 221df65, ee9e608,
ca24eb5, 1a78578, then this docs update). **ReGPU patch stack: 100** (`tools\setup.ps1 -NoBuild` must say "all 100
patches already applied"). The user's ReGPU fork github.com/braaap-stututu/ReGPU has branches **`pgr3`** (this stack,
one commit per patch, + PORT-PATCHES.md; at d1729b6) and **`fm3`** (FM3's 90), both from our pinned upstream 57143cb;
the fork's main is untouched (35 upstream commits ahead of 57143cb). Keep `pgr3` in sync when patches change: add commits
on top in the scratchpad clone (or a fresh clone), check its tree equals thirdparty/ReGPU's, push (session 5 log).

Session 5 in short (details in its log below):
- **Races render at the selected resolution** (regpu-frame-size: the guest frame size follows the front buffer, 1024x600
  in races; 1440p = native 2560x1440; upscalers/AA/ultrawide now work in races; the user confirmed race -> menu works).
  regpu-frame-room + the port's `REGPU_FRAME_ROOM_HEIGHT=600`: EDRAM surfaces sized for it from the boot.
- **Stutters:** the every-64-frames texture budget walk (regpu-texture-budget-cached), upload rings made ahead
  (regpu-ring-ahead; PGR3 starts at 256 MB), DXT texture copies on 3 helper threads (regpu-texture-copy-threads),
  index buffers write-watched instead of hashed (regpu-index-watch) + src/memory.cpp: lock-free WriteWatchClean and a
  **fault-handler race fix** (a write during a page's disarm crashed the game).
- **Upload ring: auto by default** (the user's decision): VRAM at native 1440p 4x, system memory at 720p 1x.
- The benchmark now reports per-frame **1% / 0.1% lows, p99/p99.9 and stutter counts** ("per frame:" line, result.json).

**Numbers (AI race, the user's settings 1440p 4x MSAA + CMAA 2, DXC):** cap 1000, VRAM ring: ~350-360 fps, 1% low
~200-215, 0.1% low ~165-180, p99 ~4.4-4.6 ms, no frame over 16.7 ms in the window. Cap 360 (how the user plays), auto
ring: ~330 fps, 1% low ~205-212, 0.1% low ~170, worst ~7-8 ms. Race start (before GO): one ~55 ms frame (the first race
frame: 486 textures, 4,500 first draws) and one ~19 ms. 720p 1x grid ~310-325. Absolute fps drifts a few % over an hour
on this PC: **compare only interleaved runs**.

**Where the frame goes now:** the slowest race seconds have 4,400-4,800 draws a frame, the draw thread at 100% and the
GPU at ~2.4 ms: the 1% low is ReGPU's draw-thread cost per draw (~0.8 us). Draw-thread pieces (workers) still lose 8%.

**Open decisions for the user (don't change without asking):**
1. `REGPU_INDEX_SAMPLED_HASH`: superseded by the exact index watch; probably drop the question.
2. `REGPU_NO_WAIT_DRAIN` on? (session 3: +1.3% grid). `REGPU_NO_INTERRUPT_DRAIN` is **unsafe** (crashed the boot).
3. Default frame cap: still max_fps 60 + vsync in a fresh settings.ini; raise it?
4. Two `amdxc64.dll+4BC0AA` crashes in ~75 boots with FXC (session 4): `REGPU_PSO_LOCK=1` for FXC too? (The user
   plays with DXC.)
5. Port `-mcx16` (+7-18% here) and the session-5 stutter patches to FM3?
6. settings.ini max_fps stops at 360 (the launcher too); allow more?
7. Send upstream: regpu-cf-jump-past-end, regpu-promote-notify; the fault-handler race is port code (FM3 has the same
   memory.cpp lineage? check).

**Next optimisation ideas (the draw thread's per-draw cost = the 1% low):**
1. UploadGuest: sampled ContentHash + copies ~5% (careful: buffers rewritten between draws, the FH1 skid-mark comment).
2. FrameMap<IBV>::Put/Find for index views ~2-3% (a per-frame thread_local map).
3. VertexCache copies of changed vertex data ~3% (real work; maybe helper threads like the texture copies).
4. RGBA (565/8888) texture uploads still decode on the draw thread (level 0 is decoded early for dumps/UMA).
5. Race start: the first race frame (~55 ms) and the loading-screen frame (~50 ms): one-time, low priority.
6. The game's main thread (~80% busy in races) is next after the draw thread (ReXenon options, PGO).

**How to measure (Claude may run these itself, minimized, on out\user-route, never out\user):**
- `. .\tools\benchmark.ps1; Invoke-Pgr3Benchmark -Label x -Seconds 40 -Settle 5 -Env @{ PGR3_RESOLUTION='2560x1440';
  PGR3_MSAA='4'; PGR3_AA='cmaa2'; PGR3_VSYNC='0'; REGPU_DXC='1'; PGR3_MAX_FPS='1000'; REGPU_GPU_UPLOAD_RING='1' }`
  = the user's settings at cap 1000; output line 1 = per-second summary, line 2 = **per frame** (lows, p99, stutters).
  For stutter work use cap 360 + `REGPU_UPLOAD_RING_AUTO='1'` (what the user runs). A/B = alternating variants
  (a, b, b, a), at least 2+2 runs of 40 s, launched with `run_in_background` (~90 s a run).
- Scripted runs pin `REGPU_UPLOAD_RING_AUTO=0` (tools/pgr3ctl.ps1 Start-Pgr3Dev): system memory unless -Env sets
  `REGPU_GPU_UPLOAD_RING=1` or the auto mode.
- Grid: `. .\tools\ab.ps1; Start-Pgr3AbGrid -Env @{...}`, `Measure-Pgr3Fps`, `Invoke-Pgr3BootAB` (exe copies via
  `PGR3_AB_EXE`, e.g. build\ab\s5base). The grid has no stutters (time frozen): use the race for lows.
- Profiling: `PGR3_SAMPLE_THREAD=name:gpu draw worker`, `PGR3_SAMPLE_CALLERS=2..6`, `PGR3_SAMPLE_LINES=1` (works in the
  benchmark: sum the "profile:" windows between "benchmark: measuring" and "done"). The perf lite log prints
  "long frame N" lines (over 2x the average) with a breakdown. `PGR3_LONG_STACKS=5` with
  `PGR3_LONG_STACKS_THREAD=guest 1 ` or `PGR3_SPIKES=1` sample threads in long frames, but the 1 ms sampler itself
  perturbs the race (never judge stutters with it on). GPU: `REGPU_GPU_PASSES=1`, `REGPU_GPU_SHADERS=1`.
- Correctness: `REGPU_INDEX_VERIFY=1` (index watch), `REGPU_VERTEX_VERIFY=1`; screenshots `Pgr3 "shot <path>"` and a
  pure-Python PNG diff (no PIL here; the race's opponent cars vary with boot timing, so compare like with like).
- Close the test game before every rebuild (`Close-Pgr3`). New ReGPU patch: edit thirdparty/ReGPU, then
  `.\tools\make_patch.ps1 -Name regpu-x` (it diffs against ALL listed patches: to regenerate the last one, remove its
  line from setup.ps1 first, then restore it), add it to tools/setup.ps1 + patches/README.md, `.\tools\setup.ps1
  -NoBuild`. Never `git checkout` inside thirdparty/ReGPU (revert by edit).
- Build: `cmd /c "tools\vcenv.cmd ninja -C build\rel"`; launcher: `dotnet build launcher\PGR3Launcher -c Release`.

**Other open items:** frame-rate-tied bugs at high fps (PGR3-QUIRKS G1; not checked); the rest of the race vs the
reference video (reflections, shadows, motion blur, results screen); unimplemented imports (`MmQueryStatistics`,
`XNotifyPositionUI`, `Refresh`, `XMsgSystemProcessCall`, `XamUserCreateAchievementEnumerator`, a few `Vd*`); intro skip,
DLC, saves/profiles; the launcher's benchmark button and folder picker untested; DLSS/XeSS in races untested.

Testing: the user runs the launcher or `play.cmd` (both out\user; their settings in out\user\settings.ini). Claude runs
only the scripted grid/benchmark boots (minimized, out\user-route). The user asked for autonomous optimising several
times in session 5; in a new session ask before running the game. Reference video: YouTube 5_n5CbsLcrI (0:00-7:50).

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

### Session 4 (2026-11-19): optimisation pass 2 (autonomous, user's request)
Method as in session 3 (frozen grid A/Bs: live switches interleaved, code changes as boot A/Bs of two exes; AI race
for totals). New in tools/ab.ps1: `PGR3_AB_EXE=<path>` in a variant's env boots that exe (copies of older builds with
their DLLs in `build\ab\<name>\`, so the process is still `pgr3`), e.g.
`Invoke-Pgr3BootAB ([ordered]@{ a = @{ PGR3_AB_EXE = '...\build\ab\base\pgr3.exe' }; b = @{} })`.
Start-Pgr3AbGrid no longer forces a vblank rate (uses the uncapped default).

**Results, AI benchmark race (720p 1x, uncapped, 60 s, 2 runs each):**
| Step | fps | p99 ms |
|---|---|---|
| Session 3 final | 295.2 / 296.3 | 9.4 |
| + `-mcx16` and LiveSwitch inlined, 4-lane ContentHash, relaxed draw count | 323.1 / 322.6 | 8.5 |
| + ring chunks + uncapped vblank 4000 Hz | 331.9 / 331.5 | 8.8 |
| + vblank on wait (720 Hz ticks), handoff spin, early vblank kick (session 4 final) | 338.8 / 339.4 | 8.2 / 8.7 |
Frozen grid: 248 -> ~320-340 fps (boots vary by up to ~7% on the grid: compare within a boot, or many rounds). Game
clock checked in the race at the end: 60.00 game s per 60 s. Capped play (60 + vsync) unchanged: 60 fps, vblank 120 Hz.

**The big one: std::atomic was never inlined.** MSVC's <atomic> (14.44) wraps every function in
`#pragma clang attribute push([[gnu::target("cx16")]])` (for `_InterlockedCompareExchange128`), and clang never
inlines a function into a caller with fewer target features ("conflicting target features", `-Rpass-missed=inline`).
So every `atomic::load`/store/`++` in ReGPU and the runtime was a call (431 sites for `atomic<bool>::load` alone, from
every LiveSwitch check). Fixed twice: (1) ReGPU patch `regpu-draw-thread-lean` reads LiveSwitch with `__atomic_load_n`:
grid **+10.7%**; (2) CMakeLists.txt adds `-mcx16` to every target (CMPXCHG16B is on every x64 CPU Windows 8.1+ runs
on; `-DPGR3_NO_CX16=ON` to A/B): another **+7.4%**. A higher inline threshold (`-mllvm -inline-threshold=1000`, new
cache option `PGR3_REGPU_FLAGS`) changed nothing. **The FM3 recomp's CMakeLists has no -mcx16 either: same fix likely
pays there (not touched; tell the user).**

**Adopted (ReGPU patches, all game-neutral, each with an opt-out switch):**
- `regpu-draw-thread-lean`: (1) above; ContentHash in 4 independent chains (+0.7%, boot A/B; values only compared
  within a run; `REGPU_SERIAL_CONTENT_HASH=1` old); the main context's draw count as a plain load/store instead of a
  locked add (+0.7%, 4/4; `REGPU_LOCKED_DRAW_COUNT=1` old); constants-used trace also gives read/lines/packed.
- `regpu-ring-chunks`: RingAlloc gives allocations <= 16 KB from a per-thread 64 KB chunk (one CAS per chunk, not 2-3
  a draw): +2.6% (4/4), pictures within the grid's 1-level noise; `REGPU_NO_RING_CHUNKS=1` old.
- `regpu-cp-wait-detail`: diagnostic (CP wait lines give reference/mask/counts).
- src/gpu_host.cpp: uncapped vblank 2000 -> **4000 Hz** (+1.3%, boot A/B 2/2; CP 25 -> 36% of a core).

**Why the locked instructions cost so much:** the draw thread writes ~2.3 KB a draw of constants into the
write-combined upload ring; a locked instruction (atomic RMW, CAS, SRW lock) waits for those stores to drain. A line
profile showed 8.6% on the last line of DrawImpl (a `lock xadd` on drawsThisFrame); removing it moved the wait to the
next locked op (RingAlloc's CAS, 2.7%), removing that gave +2.6%. Remaining locked ops a draw: g_textureMutex
(GetIndexBuffer, textures), FrameMap stores stalling on a full store buffer (FrameMap<IBV>::Put 2.3%).
Writing fewer ring bytes did not help (tried twice): `REGPU_CONSTANTS_BY_LINE` (whole 64-byte lines of read
constants only) -1.1% before the inlining fix, -0.2% after; removed. 84% of vertex and 92% of pixel shader draws
use packed constants (~54 / ~30 read), so by-count only touches the rest.

**Measured, not adopted:** inline threshold 1000 (0%). Noted: a live switch that changes hashes (ContentHash) must be
A/B'd by boot: flipping it re-keys every texture/upload cache and the grid drifted -5% over the run.

**Where the frame goes now (grid ~310 fps):** draw thread ~86-90% busy, its idle is WorkerLoop waiting for the command
processor. The CP is 72% in work::Drain and 11% in WAIT_REG_MEM polling. The per-frame stall: WAIT_REG_MEM on memory
0140E006 (A140E004, 32-bit swap) == 0, ~2 waits a frame, ~half block ~0.25 ms (~8% of a frame). Reading: the game
clears that flag after its one-frame throttle (sub_821507A0 -> sub_823D0D50, the block-on-fence on frame N-1) wakes,
so frame N's GPU work waits for the game's CPU work (game main thread: ~49% spinning in the throttle, ~45% real work,
e.g. sub_822291F0 12%). Not yet confirmed who writes A140E004.

**Second half: the swap's vblank wait and the drains (all in ReGPU patches `regpu-vblank-on-wait`, `regpu-cp-handoff`).**
- Who clears A140E004: a hardware watchpoint (`PGR3_WATCHW=A140E007`, crash.cpp, all physical aliases) showed
  **sub_823CFF78, D3D's vblank interrupt handler** (guest thread 4). So the per-frame wait is the swap waiting for
  vblanks (interval 2: two per frame). Timing probe (temporary): the waits end ~0.1 ms before the game's throttle
  returns, i.e. the throttle was waiting on that same chain.
- `REGPU_VBLANK_ON_WAIT` (new, on for uncapped PGR3 in src/gpu_host.cpp; `PGR3_VBLANK_ON_TICK=1` off): a blocked memory
  WAIT_REG_MEM raises the next vblank at once. Grid **+9.2%** (live 4/4, draw thread 90 -> 98% busy), 0 pixels
  different, race +2-5%; with it the tick rate is irrelevant (720/2000/4000 Hz equal), so uncapped uses 720 Hz again.
  Kicking before the wait's drain: grid +0.4% (4/4), race level (kept).
- Handoff spin (`REGPU_HANDOFF_SPIN`, default 2000 pauses before the CP/draw thread sleep on each other): race +0.9% (2/2).
- **The race's remaining sync** (REGPU_PERF drain sites): ~89% of the command processor is drains for
  `WAIT_REG_MEM mem 0140E002 (A140E000) == 4`, about twice a frame, ~1.5 ms each; the value is set by a store the CP
  queued earlier in the same stream (a scratch write-back or fence). Built "wait prediction" (skip the drain when a
  still-queued write satisfies it): 0% (the INTERRUPT right after drains instead, since its handler must see the
  draws/stores before it); with `REGPU_NO_INTERRUPT_DRAIN=1` as well, D3D's handler ran before the store ("ERR[D3D]:
  Unanticipated CPU_INTERRUPT", then an indirect call to 0BADF00D at boot). Removed. **So decision 2's interrupt half is
  unsafe** (crash), and the drains are the hardware ordering, not overhead. The interrupt handlers themselves take
  ~0.1 ms per second.
- In the race the draw thread is ~85% busy without the perf log (99% with it): the rest is most likely the game's own
  frame work (AI, physics) arriving late; not profiled yet (sample guest thread 1 in a race: PGR3_SAMPLE_THREAD=1 with
  the benchmark).

**Stability:** two crashes in ~75 boots, both `amdxc64.dll+4BC0AA` (null read on a driver thread) ~10.8 s in, while
pipelines not in the seed were built at race load, with FXC. FM3 saw the same address with DXC only (fixed there by
`REGPU_PSO_LOCK=1`, the pipeline library's loads/stores one at a time). Candidate: `REGPU_PSO_LOCK=1` for FXC too
(needs a boot series; decision 5).

**Session 4, later (user asked for a 1440p update):** AI race at 360 cap: 1440p 1x 337.5 / 337.6, 4x 337.2 / 337.2
(GPU 2.3-2.4 ms) = the same as 720p; frozen grid 1440p 4x 324 fps (GPU 2.95 ms, draw thread 96%), 4K 4x 229 fps (GPU
4.2 ms, draw thread 67%: GPU-bound). The race picture renders at 2048x1200 / 3072x1800 (1024x600 scaled, centred). A
game-thread profile in the race showed ~28% sleeping in ReGPU's PaceSwap: the race reached the 360 cap in stretches.
`PGR3_MAX_FPS` from the environment may now go to 1000 (src/settings.cpp; settings.ini still 30-360): race **391.4 /
386.6 fps** at 720p, **370.5 / 370.6** at 1440p 4x, draw thread 96-98% busy. Also: an empty XInput slot was enumerated
on every poll (`CM_Get_Device_Interface_ListW`, ~2% of the game's main thread): now asked once a second
(src/kernel/imports_xam.cpp, `PGR3_XINPUT_POLL_ALWAYS=1` old); race fps level (the game thread isn't the limit), the
enumeration gone from the profile.

**Session 4, later: launcher (user's request, "based off of our FM3 one").** `launcher/PGR3Launcher` (.NET 9, Photino.NET
4.0.16 from the local NuGet cache, WebView2), a cut-down port of the FM3 launcher: same window/scheme handler
(https://launcher.pgr3/), message bridge, stylesheet (`wwwroot/app.css` and `host.js` copied as they are) and
settings.ini handling, with PGR3's keys (src/settings.cpp) and env (PGR3_USER_DIR, PGR3_NO_TIMER,
PGR3_DRAW_THREAD_PIECES, REGPU_DEBUG + port 7342). Tabs: Play (game folder pick/check against the Europe default.xex
SHA-256, language, summary, Play), Graphics (fps cap, VSync, speed preset, upscaler, GPU, resolution, aspect,
fullscreen, MSAA, edge filter, SSAA, EQAA, anisotropy, overlays), Settings (benchmark, folders, shader cache,
testing switches incl. VRAM ring, draw workers, DXC, async present). The benchmark runs like tools\benchmark.ps1: on
out\user-route with its profile restored from out\bench\baseline and the player's settings.ini copied in for the run
(removed afterwards); results in <user>\bench. Left out of the FM3 one: disc installers, updater, profiles editor,
shader sharing, VR/triple screens, wheel tab, Linux/browser mode. `launcher.cmd` (PGR3_USER_DIR=out\user, builds on
first use); the "Project Gotham Racing 3" shortcut in the main folder opens it (*.lnk and launcher bin/obj ignored).
Checked: builds with 0 warnings; the window opens and all three tabs render (screenshots); a change is written to
settings.ini; Play starts pgr3.exe with the launcher's folders (the user closed that test start). Not checked: the
benchmark button, the folder pick.

### Session 5 (2026-11-20): research, races at the selected resolution (user's request)

**Measured** (frozen grid, 1440p, `REGPU_TRACE_RESOLVES` for ~34 frames): the race scene is a **2x MSAA surface of
pitch 1040** (1024 + tile padding), resolved as 1024x600 (colour 8888, then 2_10_10_10/float copies, and depth); bloom
at 344x200; small passes at 256x256 (pitch 280 2x, pitch 320), 512x512, 64/16/8 squares. No 1280-wide pass in the race.

**Why the race doesn't fill the screen:** ReGPU sizes everything from a fixed guest frame of 1280x720 (`kWidth`/
`kHeight`, renderer.cpp:87, 44 uses; presenter.cpp:32). `PassScalesSlow` (renderer.cpp ~500): scaleY = output
height / 720, and only a pass of pitch exactly 1280 is the "wide pass" (`WidePass()`, scaleX = output width / 1280).
PGR3's 1040-pitch scene is classed as a **helper pass** (pitch < 1280): scale 2.0 both ways at 1440p -> 2048x1200,
presented centred (regpu-front-buffer-size). Side effects of the same misclassification (read from the code, not
tested): Hor+/wide aspect, the motion vectors (`wantMotion` needs WidePass), and the scene-end detection for the
upscalers/AA filters (resolve >= 1280x720, renderer.cpp ~10796) never fire in a race. With an upscaler or post-AA on,
each race frame is taken for "a menu": the swap upscales the whole front buffer, HUD included, and retries every 120
frames (renderer.cpp ~17214). DLSS/FSR/XeSS/TAA/FXAA are effectively broken in races now.

**Options:**
- **A (recommended): a guest frame size in ReGPU** (game-neutral patch, `regpu-frame-size`): replace the constants by a
  current frame (width, height, pitch): 1280x720/1280 by default, 1024x600/1040 when the front buffer the title resolves
  is that size (auto-detected at the swap, from the next frame, with a pass-epoch bump; or set by the port; menus switch
  back to 1280x720). Then scaleY = out H / 600 (2.4 at 1440p), the wide pass (pitch 1040) scaleX = out W / 1024 (2.5):
  the race renders at **2560x1440 natively**, stretched 4% horizontally exactly like the console's scaler (assumes the
  game projects for a 16:9 display: check wheels/circles against the reference video). `OutputWide()` must stay
  relative to 16:9 (the display aspect), not 1024:600; the minimap scissor mapping (renderer.cpp ~15417) needs the 16:9
  scale (scaleWide / wide) instead of ScaleY. Host EDRAM surfaces must be sized for the larger scale (SurfaceHeight:
  1024 rows x 2.4). The other `kWidth` heuristics (scene end, `fetch.Width() >= kWidth`, front-buffer full size, debug
  shot) move to the frame size. Gains: native race picture, and upscalers/TAA/post-AA/wide aspect start working in
  races. Cost: 1.5x the pixels (GPU at 1440p 4x ~2.4 ms -> ~3.5 ms estimated: may become GPU-bound near 300 fps there).
  Also what ReGPU's own docs/xenos-notes.md anticipates ("some use 1024x600 or 1152x640 and will need this made a setting").
- **B: make the game render 1280x720** (patch its device/back-buffer setup): rejected. 1024x600 with 2x MSAA is what fits
  the 10 MB EDRAM without tiling (1280x720 2x colour+depth = 14.7 MB); the game's guest allocations (front buffer at
  01411000, bloom chain) are sized for 1024x600, a bigger resolve would overwrite memory, and every size would have to
  be found and changed. The host picture would be the same as A.
- **C: stretch the 2048x1200 picture at presentation** (the old "like the console's scaler" item): small, but upscaled,
  not native, and fixes none of the side effects. A fallback switch for A at most.

Not started: waiting for the user's go-ahead. This session's grid boot (minimized, out\user-route) was the only game run.

**Session 5, option A implemented (user: "Yes, implement A"):** new patch **`regpu-frame-size`** (95th; setup.ps1,
patches/README.md). `g_frameWidth/Height` (default 1280x720) follow the front buffer the swap shows (`NoteFrontBuffer`
-> `ApplyFrameSize` at the next frame start, next to `ApplyOutputSize`); `FramePitch` = width rounded up to 80
(1024 -> 1040). `WidePass` = pitch in [frame width, frame pitch]; big = pitch > frame pitch; helper = pitch < frame
width; scaleY = out H / frame H, scaleWide = out W / frame W; tall pass, scene end, `g_drawSamplesResolve`, clear
extension, VR/triple front-buffer checks on the frame size. `OutputWide` and `TallScaleRoom`/`SurfaceWidth` keep
1280x720 (display shape, memory). `SurfaceHeight` uses the smallest frame height seen (surfaces remade once at the
first 600-row frame: 1440p rows 2048 -> 2458). `REGPU_FIXED_FRAME_SIZE=1` (live) = old behaviour.
Verified (grid boots, 1440p, minimized, out\user-route): log "guest frame 1024x600 (pitch 1040): scale 2.500 x 2.400";
the race fills 2560x1440 (out\shots\frame1440.png); live flips 1024x600 -> 1280x720 -> 1024x600 work (fixed_on/off);
FSR 3 now runs on the race scene (1706x960 -> 2560x1440, HUD sharp: fsr3_new.png; the old exe: 2048x1200 centred).
Not yet: the fps cost (the first reading, ~110 fps, was taken while the user's FM4 session was using the GPU), race ->
menu return with a real route (the user's test), DLSS/XeSS/TAA/FXAA in races, 1080p/4K/ultrawide shapes.
**Session 5, regpu-frame-size tests (user: "do the tests now"; FM4 no longer running):** grid boot A/B, 1440p, cap 1000,
3 rounds interleaved: old exe (build\ab\fixed720) **321.2** fps (GPU 2.90 ms, draw thread 97%), new **294.1** (3.25 ms,
93%: -8.4%, the 1.5x pixels; draw thread still the main limit), new + `REGPU_FIXED_FRAME_SIZE=1` 318.1 (= old: the
switch restores it). One boot each (grid, 4 s): 1080p 326 fps (scale 1.875 x 1.8), 4K 217 (3.75 x 3.6, GPU 4.45 ms,
GPU-bound), 3440x1440 276 (3.359 x 2.4: Hor+ view, HUD in the centred 16:9 area, out\shots\s5_ruw.png), 1440p FXAA 274,
SMAA 269, TAA 236, FSR 4 (1706x960 -> 2560x1440) 204; all fill the output with the HUD sharp (s5_*.png). Not tested:
race -> menu return on a real route (the user's), DLSS (AMD GPU here), XeSS (no libxess.dll in build\rel).

**Session 5, frame spikes (user's screenshot: 358 fps, p99 7.51 ms, evenly spaced red bars; returning to menus after a
race works).** The fps panel marks frames over 2x the average red (ReGPU's long-frame rule). Reproduced with the user's
settings (AI benchmark, 1440p, 4x MSAA, CMAA 2, cap 360, DXC): p99 6.1-8 ms. Long frames come **every 64 frames**
(~4 a second): the texture budget check (renderer.cpp, `g_r.frame % 64`) called `GetResourceAllocationInfo` for every
cached texture on the draw thread under the texture mutex. `REGPU_TEXTURE_BUDGET_MB=0` confirmed it (long 0.2/s, p99
4.4). New patch **`regpu-texture-budget-cached`** (96th): sizes asked once per resource. A/B 3+3 interleaved: p99 6.1-6.2
-> 4.4-4.6 ms, long 3.8 -> 0.2/s, fps the same (~314). Left: the 30 s memory report does the same walk once every 30 s
(one long frame; `REGPU_NO_MEMORY_REPORT=1` removes it); remaining ~0.2 long frames a second not traced.
Dead ends / tools: the frozen grid has no spikes (0 long; time frozen, but the 64-frame check runs there too: its
textures are fewer). A game-thread frame probe (removed) showed the game's own frame loop steady (~5 slow frames a second,
world update 0.07 ms). `PGR3_LONG_STACKS` (sampling every 1 ms, even below its 15 ms dump threshold) itself perturbs the
race (a 30 Hz pattern of 6 ms frames): don't judge spikes with it on. New diagnostics kept (src/long_stacks.cpp):
`PGR3_LONG_STACKS_THREAD=<description start>` samples another thread (e.g. `guest 1 ` = the game's main thread), and with
`PGR3_SPIKES=1` each long frame (up to 5 a second) logs what every guest thread did (`SpikeDump` had no caller).

**Session 5, ReGPU fork (user's request):** github.com/braaap-stututu/ReGPU got two branches from our pinned upstream
commit 57143cb (the fork's main is 35 upstream commits ahead; main untouched): **`fm3`** (FM3's 90 patches) and
**`pgr3`** (PGR3's 96), one commit per patch (subject `<patch> (<PORT> port)`, body = its patches/README row), sharing the
first 81 (identical in both projects), each ending with PORT-PATCHES.md (that port's patch table). Each branch's tree
was checked equal to the project's thirdparty/ReGPU. Built in the scratchpad by a script (python, `git apply --index`
per patch). Patches changed later (e.g. regpu-texture-budget-cached below) are not on the fork until it is redone.

**Session 5, autonomous optimisation hour (user's request).**
- Draw-thread profile, 720p grid uncapped (306-323 fps, draw thread 97-99%): flat. Hashing ~8% (ContentHash in
  UploadGuest 3.4%, index FullHash 3.6%, FrameMap::Put 1.7%), VertexCache copies 1.9% (real changed data).
- Inlining the pass accessors (ScaleX/Y, PassWidth/Height, WidePass/OutputWide memoised in the snapshot; ~3% as
  calls in the profile): boot A/B 4 rounds 323.0 -> 322.5, **no gain, reverted**.
- Write-watch to skip re-hashing unchanged index buffers: **not possible** (WRONG, corrected later in session 5: the port's own page-protection watch works, see regpu-index-watch), guest physical memory is a file mapping
  (src/memory.cpp) and MEM_WRITE_WATCH only works on private VirtualAlloc memory.
- The 30 s memory report walked every texture with GetResourceAllocationInfo too (one ~6-7 ms frame every 30 s):
  now uses the kept sizes; folded into **regpu-texture-budget-cached** (regenerated; stack 96 OK). 50 s race: no long
  frame at the 30.5/60.5 s reports. Remaining long frames: texture streaming (~86 new textures in a frame, 2.5-3.5 ms
  of decoding on the draw thread, e.g. ~39.5 s into the benchmark race).
- GPU at the user's settings (1440p 4x CMAA 2, grid): 3.5 ms; the two scene passes ~1 ms each; per shader nothing
  stands out (building ps A45814C0 ~0.6 ms over ~1,040 draws).
- **Decision 3 has flipped at the user's settings (native 1440p since regpu-frame-size):** upload ring in VRAM
  (`REGPU_GPU_UPLOAD_RING=1`, settings.ini vram_upload_ring=1): grid boot A/B 3 rounds 274.7 -> 297.0 (+8.1%, GPU
  3.49 -> 2.34 ms); AI race 2+2, cap 1000: 316.7 -> 355.4 fps (+12%), p99 4.27 -> 3.83 ms. 720p 1x still prefers
  system memory. `REGPU_UPLOAD_RING_AUTO=1` picks right at both (720p: system 3.13 vs VRAM 3.33 ms; 1440p: VRAM),
  same race fps as VRAM fixed (337.1 vs 337.2, 40 s, 2+2), but each switch costs one ~21 ms frame (worst 21-22 ms vs
  6.3-6.7). Not changed: the user's decision (VRAM by default, auto, or per resolution).

**Session 5, upload ring auto (user: "Auto mode"):** src/gpu_host.cpp: `upload_ring=auto|system|vram` in settings.ini;
without it `vram_upload_ring=1` = VRAM always, else **auto** (ReGPU re-measures every 30 s, keeps the faster; one ~21 ms
frame per move). Log line "gpu: upload ring chosen by measuring (auto)". The user's settings.ini (vram_upload_ring=0)
now gets auto. Launcher: the toggle reads "Upload ring always in VRAM / Off: chosen automatically" (tooltip explains).
tools/pgr3ctl.ps1 Start-Pgr3Dev sets `REGPU_UPLOAD_RING_AUTO=0` (system memory, as all earlier measurements; -Env to
override). Verified: unpinned 1440p 4x grid boot chose VRAM at 35 s (3.20 vs 3.67 ms a frame); pinned boots log
"in system memory". Constants by count stays on (it follows vram_upload_ring != 1, as tested).

**Session 5, 1% lows / p99 / stutters (user's request).**
- **Benchmark per-frame stats:** src/hooks/grid_test.cpp records every frame time in the window (ReGPU Host::onSwap ->
  `GridTestSwap`, set in src/gpu_host.cpp) and logs "benchmark: frames N, avg, 1% low, 0.1% low (average fps of the
  slowest 1% / 0.1%), p99, p99.9, worst, stutters over 2x median / 8 ms / 16.7 ms"; tools/bench_summary.py adds them
  to result.json and prints a "per frame:" line (runs.csv unchanged).
- **The auto ring mode (made default an hour earlier) stuttered:** its first trial made the other place's rings at
  the grown 256 MB, three synchronous creations: 62/110/128 ms frames (60 s race, 1440p 4x, cap 360: 1% low 153.7,
  0.1% low 44.3, worst 128 ms, 4 frames over 16.7 ms). New ReGPU patch **`regpu-ring-ahead`** (97th): rings made on a
  background thread ahead of need (usage over 3/4; auto trials wait until the other-place rings are ready).
  -> 1% low 211.8, 0.1% low 170.8, worst 8.2 ms, 0 over 16.7 ms (auto). Half usage as the trigger was tried first:
  the race-load frame (132 MB of 256) asked for six 512 MB rings and the race start got 375/178 ms frames.
- **Race start (before GO):** every race overflowed the 128 MB ring while loading ("ring full": draws dropped) and grew
  it in the frames before GO (71/86/27/22/140 ms, the 140 ms one first-touch page faults). src/gpu_host.cpp now sets
  `config.uploadRingMB = 256` (the size it always ended at; same steady-state memory). Race start now 72/63/18 ms.
  What is left there: the first race frame decodes ~486 textures (25 ms) and the next creates ~31 render targets and
  resolve images at the new native size (~50 ms off-CPU in the driver): not fixed.
- In-race long frames left (~6 a minute over 2x the median, 5-8 ms): texture streaming, 50-154 new small textures in a
  frame decoded serially on the draw thread (large ones already use helper threads). A deferred/parallel decode would
  need the upload path restructured: not done.
- Regression check, cap 1000, system-memory ring: 320.0 fps (was 317.3 / 316.0).

**Session 5, further stutter work (user: "Can you improve it further?").**
- New ReGPU patch **`regpu-texture-copy-threads`** (98th): DXT texture levels are copied/untiled into the upload ring
  by 3 helper threads; the copy command is recorded at once and `SubmitLists` waits for the helpers. Pictures: four
  frozen-grid boots (sync, async, sync, async) match by mode except for the opponents' cars, which the race picks
  differently with the boot timing (pure-Python PNG diff in the scratchpad: pngdiff.py). Race A/B 2+2, 60 s, 1440p 4x
  CMAA 2 cap 360 auto ring: 1% low 193.9 -> 205.0, 0.1% low 157.4 -> 173.1, over 2x median 14 -> 4.5, p99.9 5.79 ->
  5.36 ms; first race frame 70 -> 53 ms.
- New ReGPU patch **`regpu-frame-room`** (99th) + src/gpu_host.cpp `REGPU_FRAME_ROOM_HEIGHT=600`: the 62 ms frame
  before GO was regpu-frame-size dropping and remaking every EDRAM surface at the first 600-row frame. Now 19 ms.
  Race start now: first race frame ~55 ms (486 textures, 4,500 first draws), then 19 ms; the loading-screen frame at
  ~11 s ~53 ms (195 textures).
- In-race: one frame over 2x the median left in a 60 s run (156 new textures, mostly resource creation). The 1% low
  (~195-210 fps, ~5 ms frames) is now heavy scenes with the draw thread the limit, not stutters. Not done: the RGBA
  (565/8888) upload path still decodes on the draw thread (its level 0 is decoded early for dumps/UMA).

**Session 5, draw-thread work for the 1% lows (user: "continue ... 1% lows and p99").** The slowest race seconds have
4,400-4,800 draws a frame, the draw thread at 100% and the GPU at ~2.4 ms: the 1% low is the draw thread's per-draw
cost. Race profile (summed over the run): index buffer work ~11% (lookups, per-frame map, full hash), memcpy ~8.6%
(VertexCache copies of changed vertex data, UploadGuest, CP register copies: real work), write-watch lock ~3.9%.
- My session-5 note that write-watching is "not possible" was wrong: the port already has a page-protection watch
  (src/memory.cpp WriteWatchArm/Clean, a vectored exception handler) that the vertex arena uses. New ReGPU patch
  **`regpu-index-watch`** (100th) uses it for index buffers (no full hash while clean; back-off after 4 same-content
  re-arms; `REGPU_INDEX_VERIFY=1` diagnostic: 25 M + 21 M clean answers verified in two races, 0 misses).
- src/memory.cpp: **`WriteWatchClean` lock-free** (atomic bits + aligned 64-bit stamps; `PGR3_WATCH_CLEAN_LOCKED=1`
  start-time = old), and a **fault-handler race fixed**: Disarm clears a page's bit before making it writable, and a
  write faulting in between (or between the views' VirtualProtect calls) was passed on as a crash. It happened once
  (guest thread 21, sub_821B0AA8, in an A/B run) once index buffers were watched too; the not-armed path now checks
  the page under the shared lock (waits for a disarm in progress). 8 races since: no crash.
- Result, interleaved 2+2 on the final code (1440p 4x CMAA 2, cap 1000, VRAM ring): 334.2 -> 352.9 fps, 1% low
  182.3 -> 201.1, 0.1% low 151.2 -> 164.4, p99 5.03 -> 4.56 ms.
- Draw-thread pieces (`PGR3_DRAW_THREAD_PIECES=1`) re-tested with the watch: still -8% (319.8 vs 347.4), lows worse.
- The machine's absolute numbers drifted a few % during this hour (the same configuration: 340 then 334): compare
  only interleaved runs.
- Next candidates: UploadGuest (sampled ContentHash + copies, ~5%; careful: mid-frame rewrites, FH1 skid marks),
  FrameMap::Put/Find for index views (~2-3%), the VertexCache copies of dynamic vertex data (~3%).
