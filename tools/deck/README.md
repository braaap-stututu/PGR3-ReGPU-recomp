# PGR3 on the Steam Deck (session 6, 2026-11-25)

Builds run in the **`regpu` distrobox** (Arch; clang-cl + lld-link + xwin MSVC/SDK, set up for ReGPU-V:
`/home/deck/claude projects/ReGPU-V/work/scripts/deck/README.md`). The game runs through Proton 11.0.

## Build

```sh
cd "/home/deck/claude projects/PGR3ReGPU"
distrobox enter regpu -- bash -c '. "/home/deck/claude projects/ReGPU-V/work/scripts/deck/env.sh";
  export PATH="$PWD/tools/deck/fxc:$PATH"; ninja -C build/deck -j6 pgr3'
```

Configured once with:

```sh
cmake -S . -B build/deck -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$REGPU_TOOLCHAIN" \
  -DREGPU_D3DCOMPILER="$PWD/build/redist/d3dcompiler_47.dll" -DPython3_EXECUTABLE=/usr/bin/python3
```

- `build/redist/`: d3dcompiler_47.dll, dxcompiler.dll, dxil.dll (copied from the Windows build/rel).
- `tools/deck/fxc/`: **fxc stand-in**. ReGPU embeds three compute shader sets as DXBC (`embed_hlsl.py` calls `fxc`),
  and the Linux dxc can't make DXBC. `fxc_shim.exe` (our own, `fxc_shim.c`, cross-built with `xclang-cl /O2 /MT`)
  calls `D3DCompile` in d3dcompiler_47.dll, run through Proton's Wine; from inside the distrobox it hops to the host
  (`distrobox-host-exec`: Proton's Wine needs SteamOS's libraries) and runs one compile at a time (flock). Output
  checked byte-identical to the Windows build's headers (fsr1, postaa, fxaa, smaa, cmaa2).
- Full build ~6 min at -j6; a ReGPU file change ~1 min.

## Run

- `tools/deck/play.sh [VAR=value ...]`: the game on out/user (Deck defaults 1280x800, MSAA 1x, cap 60 + vsync;
  `PGR3_DECK_DEFAULTS=0` = settings.ini as it is). Prefix `~/.local/share/pgr3-regpu/prefix`.
- `tools/deck/gridshot.sh <out.png> [VAR=value ...]`: Claude's scripted grid boot (out/user-route, profile restored
  from out/bench/baseline), screenshot through the debug server (port 7342), then closes the game. `PGR3_KEEP=1`
  leaves it running (close with `regpuctl.py quit`); `PGR3_EXE=` another exe. 1280x720, MSAA 1x, cap 60.
  The route sometimes misses the menu and ends in the attract movie ("no race after 4 minutes"): boot again.
- `tools/deck/bench.sh <label> <seconds> [VAR=value ...]`: the AI-driven benchmark race (as benchmark.ps1) at 1280x800,
  MSAA 1x, uncapped; results in out/bench/<label>-<time>/ and a line in out/bench/deck-runs.txt. Interleave A/B runs.
- `tools/deck/menus.sh menu|shot|race ...`: menu driving for checking other cities and modes (Playtime routes, day/night)
  on out/user-route; see its header. Used for the session-6 survey (NOTES.md).
- `tools/deck/make_patch.sh <name>` / `make_patch.sh -`: bash port of make_patch.ps1 / the stack check.
- Debug server: `REGPU_DEBUG_PORT=7342 python3 -I thirdparty/ReGPU/tools/regpuctl.py <cmd>`; paths for `shot` and
  `hlsl` are Windows paths (`Z:\tmp\...`).

## The Vulkan ReGPU build (session 6, 25 Nov)

PGR3 also builds against **ReGPU-V**, the Vulkan ReGPU the FM3 and FM4 ports use (github.com/braaap-stututu/ReGPU):
`thirdparty/ReGPU-vk` is a clone on the local branch **`pgr3vk`** = origin/fm4deck (FM3 + FM4 Deck work) + origin/main
+ PGR3's fixes as commits (control flow end-pair target = D3D12 patch 101, tiny passes at 1:1 = 102, frame room, texture copy threads, the REGPU_DUMP_SPV diagnostic).
Of the D3D12 stack's 102 patches, 85 were already in ReGPU-V (the November merge took the FM3/PGR3 stack); the rest
are D3D12-only (PSO lock, DXC tuning) or D3D12 ring work (ring-ahead) not ported; texture copy threads were ported (a pgr3vk commit).

The clone is not tracked here (it's its own repo, and `pgr3vk` isn't pushed). Its whole branch is saved in this repo as
`patches/regpu-vk/pgr3vk.patch` (one diff from fm4deck `d5a5a65`, which is on GitHub; commit list in
`pgr3vk-commits.txt`). To rebuild the clone:

```sh
git clone -b fm4deck https://github.com/braaap-stututu/ReGPU thirdparty/ReGPU-vk
cd thirdparty/ReGPU-vk && git checkout -b pgr3vk d5a5a65 && git apply ../../patches/regpu-vk/pgr3vk.patch
```
After more ReGPU-V work, export it again: `git diff --binary d5a5a65 pgr3vk > ../../patches/regpu-vk/pgr3vk.patch`.

```sh
cmake -S . -B build/deckvk -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$REGPU_TOOLCHAIN" \
  -DPGR3_REGPU_DIR="$PWD/thirdparty/ReGPU-vk" -DREGPU_DXC_EXE=/usr/bin/dxc -DPython3_EXECUTABLE=/usr/bin/python3
ninja -C build/deckvk -j6 pgr3          # (no fxc needed: ReGPU-V embeds its compute shaders as SPIR-V via dxc)
```

- CMake: `PGR3_REGPU_DIR` picks the ReGPU (Vulkan when it has src/gpu/vk); then the FidelityFX `_vk` DLLs and
  `thirdparty/dxc/bin/x64` (dxcompiler.dll + dxil.dll with SPIR-V output; a link to the FM3 Deck tree's) go beside the exe.
- Source: `src/gpu_memory_log.cpp` uses `regpu::GetGpuMemory` under `PGR3_REGPU_VULKAN`; `src/log.cpp` has a no-op
  `LogStart` (otherwise ReGPU-V's own log.cpp is linked in and clashes).
- Run: `PGR3_VK=1 tools/deck/play.sh`, `PGR3_VK=1 tools/deck/gridshot.sh ...`. Its pipeline cache is
  `vk-pipelines-1002-1435.bin` in the user folder (beside the D3D12 caches).
- The Deck's 1280x800 renders 1280x720 with bars (views narrower than 16:9 aren't supported). In the race the Deck
  averages ~72 fps uncapped (1% low ~35 after session 6's I/O fixes); with play.sh's Deck defaults (cap 60 + a 240 Hz guest vblank) it holds 60 fps (with XMA codec reuse):
  see NOTES.md.
- `tools/deck/threads.py [seconds] [top]`: CPU per thread of the running game (from the FM4 Deck tools).

## Telemetry, triple screen, VR (session 7, 25 Nov late)

- **Telemetry:** `PGR3_TELEMETRY=127.0.0.1:5300` (settings.ini `telemetry=`, `telemetry_format=fm7|fh`, `telemetry_hz`):
  Forza "Data Out" packets (SimHub's Forza Motorsport 7 game). Layout and what is filled: src/hooks/telemetry.cpp.
- **Triple screen:** `tools/deck/gridshot.sh out/shots/t.png PGR3_TRIPLE=1 PGR3_RESOLUTION=3840x720` (three 1280x720
  screens; ~42 fps on the Deck's frozen grid). settings.ini `triple=1` + FM3's triple_* layout keys.
- **VR without a headset:** `PGR3_VR=cockpit PGR3_VR_ANY_CAMERA=1 PGR3_VR_FAKE="yaw pitch roll x y z"` (degrees, cm;
  live through the debug server's `env PGR3_VR_FAKE ...`). Builds with OpenXR when thirdparty/openxr has include/ and
  bin/openxr_loader.dll (see thirdparty/openxr/SOURCE.txt on the Deck).
- Debug: `env PGR3_CAMERA_TRACE 40` (the frame's view hand-offs), `switch REGPU_TRACE_TRIPLE 1` (ReGPU-V: the
  recording, swap and side replays), `switch REGPU_TRACE_ZPD 1` (occlusion query counters).
