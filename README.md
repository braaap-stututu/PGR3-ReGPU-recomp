# PGR3-ReGPU recomp

Work-in-progress static recompilation of **Project Gotham Racing 3** (Xbox 360, Bizarre Creations, 2005) for Windows
x64, built on [ReXenon](https://github.com/zer0ducksgiven/ReXenon) (the recompiler) and
[ReGPU](https://github.com/zer0ducksgiven/ReGPU) (Xbox 360 GPU on Direct3D 12). It follows the
[PGR4-ReGPU](https://github.com/zer0ducksgiven/PGR4-ReGPU) port and our Forza Motorsport 3 ReGPU recomp.

**No game data is included.** You need your own copy of the game (Europe disc, title 4D5307D1).

## Status

- Recompiled: 16,630 functions, no unresolved branches.
- Boots through the intro movies, menus, car purchase and garage into a race, with sound. The London race matches the
  console.
- Races render at the resolution you pick (the console drew them at 1024x600 and stretched them): native 2560x1440
  at 1440p, with the upscalers, anti-aliasing filters and ultrawide working in races.
- The console's 30 fps lock is removed: an AI-driven benchmark race runs at ~350 fps uncapped at native 1440p with 4x
  MSAA on an RX 7900 XTX (29.7 fps locked), 1% low ~200 fps, no stutters over 16.7 ms during the race.
- Runs on the Steam Deck (Vulkan ReGPU; `tools/deck/README.md`).
- Telemetry out (settings.ini `telemetry=127.0.0.1:5300`): the player's car in Forza's "Data Out" UDP layout
  (motion, engine, gear, inputs, wheels, suspension, lap, position, lap times), for SimHub, motion rigs and dash apps.
- True triple screen (settings.ini `triple=1`, resolution = the three screens together, e.g. 7680x1440): each side
  screen drawn with its own turned view, HUD on the centre screen. Checked on the Deck's Vulkan build.
- VR (experimental, not yet tried in a headset; needs thirdparty/openxr): settings.ini `vr=screen|cockpit|stereo`;
  cockpit head tracking checked with a simulated head pose.
- Still to do: the rest of the race against the console, a few system calls, intro skip, DLC, saves.

## Layout

| Path | What |
|---|---|
| `src/` | Runtime: kernel and XAM, memory, threads, files, audio, input, settings, and the game hooks (`src/hooks/`) |
| `config/` | ReXenon configuration: function boundaries and jump tables for the disc `default.xex` |
| `patches/` | Our patch stacks on ReGPU and ReXenon (`patches/README.md` lists each one and why) |
| (fork) | The ReGPU stack as commits: github.com/braaap-stututu/ReGPU, branch `pgr3` (one commit per patch) |
| `tools/` | Setup, recompile, XEX/ISO tools, debug-server client, test grid, benchmark and A/B scripts |
| `launcher/` | The launcher (.NET 9, Photino + WebView2), ported from our FM3 one: Play, Graphics, Benchmark, Settings |
| `thirdparty/` | ReGPU, ReXenon, meshoptimizer (submodules; patched by `tools/setup.ps1`) |
| `NOTES.md` | Session notes: current state, findings, every fix and measurement |
| `PLAN.md`, `PGR3-QUIRKS.md` | The plan and the game's known quirks |

## Building (Windows)

VS 2022 Build Tools, LLVM clang-cl, CMake, Ninja, Python 3.

```powershell
py -3 tools\xiso.py "<PGR3 disc>.iso" extract game\disc   # the game files (never committed)
copy game\disc\default.xex game\default.xex
.\tools\setup.ps1             # submodules, patch stacks, host tools
.\tools\recomp.ps1 -Base      # game\default.xex -> out\ppc (checks the Europe disc's SHA-256)
cmd /c "tools\vcenv.cmd cmake -S . -B build\rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
cmd /c "tools\vcenv.cmd ninja -C build\rel"
launcher.cmd                  # the launcher (builds it on first use; needs the .NET 9 SDK), or:
play.cmd                      # straight into the game; both keep saves and settings in out\user, the log in out\run\pgr3.log
```
