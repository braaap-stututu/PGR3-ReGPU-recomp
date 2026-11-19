# PGR3 ReGPU recomp: plan (draft)

Static recompilation of **Project Gotham Racing 3** (Bizarre Creations, Xbox 360 launch title, Nov 2005)
for Windows x64 on ReXenon/XenonRecomp + ReGPU (D3D12), following the PGR4-ReGPU port
(<https://github.com/zer0ducksgiven/PGR4-ReGPU>) and our FM3-ReGPU recomp
(`G:\claude code projects\FM3-ReGPU recomp`).

## Status (end of session 4, 2026-11-19)

Phases 0-4 done: recompiled (16,630 functions, 0 unresolved), boots to a race that matches the console on the London
grid. Phase 5 (rendering parity) partly: race frame scaling and the rest of the race still to check. Phase 6 under way:
30 fps lock removed, test grid + AI benchmark (FM3 style), two optimisation passes 29.7 -> 296 (session 3) -> 339 fps
uncapped at 720p (389 with the cap lifted); a launcher (launcher.cmd) ported from FM3. Frame-rate-tied bugs (G1) not
checked yet. Details and next steps: NOTES.md "Start here".

## Strategy in one paragraph

Use **PGR4-ReGPU as the template, not FM3**. PGR4 is the same studio and engine line, so its findings
should mostly carry over: data formats (`.pak/.bpr/.fsb/.bik`), FMOD mixing, the XMA lock register,
the 60 Hz physics thread, fence behaviour, predicated tiling and VFS mappings. From FM3 we take the
**process and tooling**: `setup.ps1`/`recomp.ps1`/check-script pipeline, the scratch-index ReGPU
patch stack, ReXenon as default recompiler, `fm3ctl`-style debug-server client, frozen-grid boot hook,
perf suite, launcher/installer polish, and the CLAUDE.md working rules. FM3 went from setup to a
driveable race in ~8 sessions because its FM4 template matched its engine; PGR4 → PGR3 should be
the same kind of match. The difference is that PGR3 is a **2005 launch title built on a very early
XDK**. Expect different kernel/XAM imports, older D3D/XMA library code and early-XDK compiler output
(new VMX patterns, odd jump tables).

## Phase 0 findings (session 1; details in NOTES.md)

Where PGR3 turned out **different from PGR4**, which changes the plan:
- Title 4D5307D1, XDK 2.0.**2099** (launch era). The plan was base XEX + Title Update #5, but **TU5 doesn't apply
  to the Europe disc XEX** (image key mismatch). The disc XEX was built 2006-10-05, so it is a later pressing
  with the updates folded in; TU5 patches the 2005 launch XEX. Target: the **disc XEX** (`recomp.ps1 -Base`).
- UI scripting is **Lua 5.0.2**, not 5.1: PGR4's Lua hooks need rewriting.
- Audio is **XACT wave banks + XAudio**, read by the game's own loader. **No FMOD**. Music is WMA via XMP (same as PGR4).
- Data packs are `.xpr`/`.pak`/`.stream2`, not `.bpr`. PGR4's boot screen built from `.bpr` art won't apply.
- D3DX9's shader compiler is linked, so some shaders may be compiled at runtime.
- Debug tiling modes: 1280x720 2×MSAA 2 tiles (horizontal/vertical) and 4×MSAA 4 tiles.
- 265 imports (PGR4: 293). Bink is compiled into the XEX, as in PGR4. Three DLC car packs, read via `DLC:\`.

## Phase 0: Prerequisites and research (1–2 sessions)

1. **Disc dump.** Get a PGR3 disc image (user supplies). Record title ID, `default.xex` version and
   SHA-256, and any title updates (`.xexp`). Decide whether to target the base XEX or the last TU (PGR4
   ships TU2 via xexp; do the same if a TU exists). Pin the SHA in `recomp.ps1`.
2. **Bring PGR4 up to date first.** The local clone in `PGR4 ffb research\PGR4-ReGPU` is one squashed
   commit from 2026-11-05. Upstream is at 0.2.7 with ~35 commits, and the local tree has uncommitted FFB
   work (`src/physics/`, `hooks/telemetry.cpp`). Keep the FFB work on a branch, pull upstream, and treat
   upstream `main` as the template.
3. **Reference baseline.** If a Xenia/ReXGlue build of PGR3 exists, set it up as the "known good"
   picture, like hl360 for FM3. Otherwise use Xenia Canary plus console footage. Write down how it looks
   (resolution, AA, HUD, bloom) to compare against.
4. **`PGR3-QUIRKS.md`.** Catalogue known PGR3 emulation issues (Xenia compat tracker, forums), plus every
   PGR4 quirk with a guess at whether it applies to PGR3:
   - XMA lock register `0x7FEA1818` rotation; physical addresses in XMA contexts
   - mftb overflow (`patch_mftb.py`)
   - car-shadow frustum byte and `grid_bounds` infinite loop (PGR4 game bugs exposed by recomp)
   - fence/buffer reuse → "truthful fences" + `present_wait` hook
   - frame-rate-tied logic (gear latch, garage walk, menu repeat)
   - Lua UI (check whether PGR3 already used Lua 5.1)
   - Bink built into the XEX (check for a `BINK` section)
   - static shader microcode in the XEX (no packet header)
5. **Facts to check early, unknown until we look:**
   - Render resolution and AA. PGR3 is widely reported to render below 720p (often quoted as
     1138×640, 2×MSAA) and upscale. That affects predicated tiling and the tile count, and whether an
     "internal resolution" override is wanted.
   - Audio middleware (FMOD as in PGR4?) and music format (XMP/WMA? custom soundtrack support?).
   - Gotham TV / Live / leaderboards: all stubbed offline as in PGR4.
   - Kudos, photo mode, garage/Gotham-walk scenes: extra render paths to test.

## Phase 1: Skeleton (1 session)

- `git init`, `.gitattributes` with `*.patch -text` (CRLF broke every patch in FM3), `.gitignore`
  keeping `game/`, `dumps/`, `reference/`, ISOs and `out/` out of git.
- Submodules: ReXenon, XenonRecomp (hedge-dev), ReGPU (zer0ducksgiven), plus the local FidelityFX
  junction and FFmpeg LGPL shared build (`thirdparty/ffmpeg`).
- Copy from FM3 and rename `fm3`→`pgr3`: `tools/setup.ps1`, `recomp.ps1`, `vcenv.cmd`, `genstubs.py`,
  `patch_mftb.py`, `hosttools/xexdump.cpp`, `ppcdis.cpp`, `CMakeLists.txt` (clang-cl only, force-include
  `ppc_hooks.h`, `/STACK:0x1000000`, `PGR3_PPC_ONLY`, `PGR3_RECOMPILER`).
- Recompiler patches: FM3's 5 XenonRecomp + 1 ReXenon patches, **plus a diff against PGR4's
  `patches/xenonrecomp.patch`** (bdnzt, mtlr/blrl, vcmpbfp, vsel128, vctuxs, lvehx…). Whatever ReXenon
  doesn't already cover becomes a ReXenon patch.
- ReGPU: start from the **union of PGR4's settings and FM3's ~83 patches**. Every patch is behind a
  `REGPU_*` switch, so they can all be applied and then turned on one by one. Begin with PGR4's
  `regpu_host.cpp` defaults (draw workers, truthful fences, 512-draw submits, 256 MB upload ring).
- `CLAUDE.md` adapted from FM3's: NOTES.md "Start here (session N)" protocol, user drives via
  `play.cmd`, measure numerically, never `git checkout` inside `thirdparty/ReGPU`, never link PGR4/FM
  `sub_XXXXXXXX` addresses, don't execute other repos' code, saves only touched while the game is closed.

## Phase 2: Recompile to zero errors (1–4 sessions; most of PGR4's early pain was here)

1. `xexdump` → `out/pgr3.bin` + sections, then decrypt and decompress (and apply the TU xexp if there is one).
2. `tools/pgr3_config.py` (merge FM3's `fm3_config.py` and PGR4's `findhelpers.py`/`datafuncs.py`):
   - save/restore helpers (8 patterns)
   - setjmp/longjmp, matched against the CRT copies in FM2/PGR4. An early-XDK CRT may differ, so
     confirm in `ppcdis`
   - `.pdata` exception words → `invalid_instructions`
   - functions reached only through data, leaves without `.pdata`, `.pdata` functions split mid-way
3. XenonAnalyse with FM4's back-trace patch → `config/pgr3_switch_tables.toml`. Then
   `switchfix.py`/`missedswitch.py` for byte-offset tables that need explicit sizes.
4. `config/pgr3.toml`: hand-written, every entry commented. Fibers: check whether PGR3 uses XAPI
   fibers (FM3 did, PGR4 didn't).
5. `tools/pgr3_check.py` → `out/check.txt`: unresolved branches, traps, functions running off their end,
   pointer targets that aren't function starts. Iterate to 0, then `PGR3_PPC_ONLY` compile check.
   - Watch for mis-sized thunks swallowing a neighbour. PGR4's crowd flash bug was one: it compiled
     cleanly and failed only at runtime.

## Phase 3: Runtime and first boot (1–2 sessions)

- Copy PGR4's `src/` runtime: kernel, loader (with xexp), memory, threads, VFS, XAM, crypt, audio,
  xma/xmp, crash/hang, settings, user_dir, zip, static_shaders, boot_screen. Bring in FM3's later
  improvements that are game-neutral: `live_switch`, `log` clock, `zip_overlay`, `trace_xam`,
  `indirect` fatal-miss reporting.
- **Import diff:** list PGR3's kernel/XAM imports with `imports.py` and compare with PGR4's 293. A
  2005 XDK will import things PGR4 doesn't, and with different semantics; implement the gap. Weak
  stubs from `genstubs.py` log every unimplemented call on first use.
- VFS: same mappings as PGR4 (`game:`, `d:`, `\Device\Cdrom0`, `Harddisk0`, `cache:`, `\Device\Content`,
  `update:`).
- Goal: boot to logos/attract movie with audio. Expect the first problems in timebase, XMA
  (lock register and physical addresses) and missing imports.

## Phase 4: Menus → first race (1–2 sessions)

- Input: XInput pads via XAM first; wheel later.
- **`grid_test` hook early.** Boot straight into a race and freeze the world on the grid. This is the
  automated test spot for every later rendering and performance session (FM3 tip: it saves a huge
  amount of time). Also add skip-intro (Bink MOVIE_FINISHED trick from PGR4) and a dev command line.
- Debug-server client `tools/pgr3ctl.ps1` (from `fm3ctl.ps1`): status, capture, shader tint/isolate,
  peek/poke, shots, virtual pad. With the frozen grid, Claude can run the test loop without the user.
- Goal: user drives a race to the finish with picture, input and audio.

## Phase 5: Rendering parity (open-ended; the long tail in both FM3 and PGR4)

Check against the baseline, in this order of likelihood based on both ports:
1. Predicated tiling / tile seams / per-tile bytes per pixel (PGR4's black bottom third) and MSAA
   resolve scaling.
2. Resolve aliasing and CPU write-backs of render targets: reflections, mirrors, photo mode,
   garage thumbnails.
3. Occlusion-query flares: headlights, brake lights, sun (both PGR4 and FM3 hit these).
4. Fences and buffer reuse: torn geometry, crowd skinning.
5. Motion blur, bloom/HDR, the `2_10_10_10` front buffer, gamma.
6. Upscale: if PGR3 renders below 720p, add an internal-resolution override (hook the game's
   back-buffer setup) so it renders at the host's resolution rather than upscaling.

Every fix is either a game-neutral ReGPU patch behind a `REGPU_*` switch or a PGR3 rule in
`gpu_host.cpp`, and gets a row in `patches/README.md`.

## Phase 6: Frame rate, timing and performance

- Find the physics thread and step. If PGR3 has PGR4's fixed 60 Hz physics with 30 fps rendering,
  port the decoupling plus `gear_latch`, menu-repeat and walk-quantisation fixes, then re-address them.
- Default cap 60, raise only once the frame-rate-tied bugs are found.
- Perf tooling from FM3 (`perf_suite.ps1`, `bench_matrix`, `REGPU_PERF_LITE`, PGO). Expect the
  command processor waiting on fences to be the limit, as in PGR4.
- Ship a pipeline seed (`tour.ps1` → `pipeline_seed.py`) so first-boot shader compile doesn't take
  minutes.

## Phase 7: Features and packaging

- Launcher: fork PGR4's (Photino; ISO install, profiles, 360 save import, wheel bindings), updated with
  FM3's later pages (GPU choice, benchmark, Linux runner). Installer: Inno Setup.
- Saves/profiles: XContent under `profiles\<name>\content`, offline XUID, fixed-key XeKeys (PGR4).
- Wheel/FFB: SDL3 wheel driver from PGR4/FM. Check which `XInputdFF*` effects PGR3 sends. If PGR3
  sends no steering force like PGR4, reuse the telemetry → `ffb_model` approach from the PGR4 FFB
  research once that work has landed.
- DLC/marketplace content if PGR3 has any; split-screen; photo mode.
- Optional later: VR / triple screen hooks from FM3.

## Risks

| Risk | Why | Mitigation |
|---|---|---|
| Early-XDK code gen | launch titles predate later compiler/XDK fixes; new VMX128 patterns, odd tables | budget Phase 2 like PGR4 (longest phase), not FM3 (one session) |
| Kernel/XAM differences | 2005 XDK libraries (D3D, XMA, XAudio, XInput) differ from 2007 | import diff in Phase 3; read the guest library code before copying PGR4 semantics |
| Sub-720p render + tiling | resolution/tile layout differs from both templates | baseline capture in Phase 0; per-tile checks in ReGPU |
| No good reference baseline | Xenia support for PGR3 may be weaker than for FM3 | console footage, game's own data (`.bpr` art) to check textures |
| Template drift | PGR4 local clone is stale and has uncommitted FFB work | pull upstream first; branch the FFB work |

## Done looks like (MVP)

Boots from the user's own disc dump, skips the intro, menus work, a full race at 720p+ with
correct picture, audio and music, saves and profiles persist, 60 fps with no frame-rate-tied bugs,
packaged with a launcher. Everything after that (higher fps, wheel FFB, upscaled internal res, VR)
is roadmap.

## First concrete steps

1. User provides the PGR3 ISO (and TU if any) → record title ID / XEX SHA.
2. Update the PGR4 template (pull upstream, branch local FFB work).
3. Session 1: Phase 0 research → `PGR3-QUIRKS.md`, import list, resolution/tiling facts.
4. Session 2: Phase 1 skeleton + first `recomp.ps1 -Prepare` run.
