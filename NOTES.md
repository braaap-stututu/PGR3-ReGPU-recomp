# PGR3 ReGPU: notes

## Start here (session 10)

**Session 9 (2026-11-27, Deck): the race glitches are fixed; a write-watch crash fixed.**
- **Glitches (vanishing building parts, blue/purple faces, dark blotches, every lap): cause = `REGPU_TEXTURE_CHECK_STAGGER`**,
  which src/gpu_host.cpp switched on by default (session 13 PC perf work, "+4%"). PGR3 streams scenery into reused
  memory; a texture rechecked only every 4th/16th frame showed the previous texture's data for a few frames. Now OFF
  by default (`REGPU_TEXTURE_CHECK_STAGGER=1` brings it back); `REGPU_DIRECT_DYNAMIC_INDICES` stays on.
  Found with the user's capture-card A/B (tools/deck/race_ab.sh, one live switch per lap, London AI race):
  - ab1: lap 2 `REGPU_SYNC_TEXTURE_COPIES=1`, lap 3 `REGPU_ZPD_STRICT_END=1` -> glitches in all 3 laps (both
    session 8 suspects cleared).
  - ab2: crashed before the first flip (below).
  - ab3: lap 2 both draw shortcuts off -> clean; lap 3 shortcuts on + `REGPU_NO_VERTEX_ARENA=1` +
    `REGPU_INDEX_NO_WATCH=1` -> bad (and 49 fps: the arena off is expensive).
  - ab4: lap 2 only the stagger off -> clean; lap 3 stagger on, direct indices off -> glitches.
  - ab5 (fixed build, nothing switched): the user saw no glitches. 57.6 fps avg, 1% low 34.6, 0.1% low 26.2
    (ab1 with the stagger on: 57.6 / 32.9 / 19.7). Per-lap in ab4: 58.8 fps (on) vs 57.8 (off), draw thread 69 vs 68%.
  - The D3D12 build shares src/gpu_host.cpp: it gets the same default at its next Windows build.
- **Crash fix, src/memory.cpp OnWatchedWrite:** ab2 crashed ~37 s after GO (C0000005, write to guest AA491000, guest
  thread 23, lr 821B0AB0 = sub_821B0AA8, the same site as the session 5 crash). The not-armed path checked "armed"
  and "writable" in two separate lock holds; a re-arm in between (read-only again, bit set) was passed on as a crash.
  Both are now read under one shared lock and an armed-again page is retried; a write still passed on logs
  "write watch: write to page ... passed on" with its protection. Seen once in ~6 Deck races; ab3-ab5 ran clean.
- out/shots/scan-base (4.4 GB of session 8 frames) deleted: no longer needed.
- **PGR4 ports + lows work (user asked: port PGR4-ReGPU's optimisations, chase the remaining lows leads).** Upstream
  github.com/zer0ducksgiven/PGR4-ReGPU at 24e0fa9 (26 Nov; reference/PGR4-ReGPU is the 8 Nov copy). Most of PGR4's
  Nov work was already in ReGPU-V (vertex reach, ring chunks, reg/shader-repeat skips, NOP filter, CP idle sleep,
  alpha-test split, GameSettled, NoInitAllocator). Ported into thirdparty/ReGPU-vk (branch pgr3vk, commit 343252d, pushed):
  - fence submits on the replay thread (eb31e61; `REGPU_SYNC_FENCE_SPLITS=1` live = old), FenceSubmitImpl ->
    SplitMainListAsync(true, value) + QueueClosedSubmitAndSignal;
  - draw-piece workers above normal (6adffd9; `REGPU_WORKER_PRIORITY=n`);
  - index buffers on their own lock (591e573; `REGPU_INDEX_TEXTURE_LOCK=1` = old);
  - dynamic buffers hashed outside the lock (8033301; `REGPU_DYNAMIC_HASH_LOCKED=1` = old).
  Deck bench (tools/deck/bench.sh, 1280x800 uncapped, 120 s, 3 interleaved pairs): 83.6 -> 84.2 fps, frames over
  16.7 ms 435 -> 386, 1% low 45.5 -> 45.9. Kept. (The piece workers are ~1% busy on the Deck: little to win there.)
  Not ported: Copy128 (FX-only), async frame end (no gain in PGR4), PGR4's pipeline seed (PGR3 has its own).
- **Thread pinning (PGR4 de4c40e) as opt-in `PGR3_PIN=auto|m:..,c:..,*:..`** (src/kernel/thread.cpp): worse on the
  Deck (2 x 200 s: 80.2 -> 78.9 fps, 1% low 43.8 -> 40.4): 4 cores and PGR3's draw thread. Stays off.
- **Worst frames = VMA growing by a 256 MB block** (~30 ms vkAllocateMemory in the frame: VRAM +256 MB exactly at
  each). Made-ahead on a helper thread was tried and dropped (the frame still stalled while it ran). Fix: ReGPU-V
  `REGPU_VMA_BLOCK_MB` (vk/device.cpp), PGR3 defaults it to 32 (src/gpu_host.cpp). 2 x 200 s each, interleaved:
  0.1% low 27.0 -> 33.8 fps, worst 46-55 -> 37 ms, 1% low 42.3 -> 44.2, avg 79.9 -> 80.6; biggest texture-create
  stall 33 -> 3-6 ms (64 MB: about the same).
- Per-thread CPU in an uncapped race (tools/deck/threads.py): draw thread 97%, game main 94%, audio driver 35% (the
  game's mixer), xma decoder 26%, guest 25 28%, replay 21%. Both top threads are bound; audio is real work.
- Final check at the user's settings (race_ab.sh final60, 720p vsync 60, no switches): 57.5 fps, 1% low 33.2,
  0.1% low 26.1 (ab5 before the ports and 32 MB blocks: 57.6 / 34.6 / 26.2): vsync hides the gains. Its worst frames
  are now the game's (58 ms with the draw thread idle 41 ms: waiting on the title) or the GPU (gpu wait 28 ms);
  texture creation <= 1.5 ms. No crash.
- **Audio (user: anything from FM2-ReGPU / PGR4 for PGR3's audio?).** FM2-ReGPU's source IS on the Deck:
  `/home/deck/claude projects/ReGPU-V/work/Forza-Motorsport-2-ReGPU` (dckthulhu/Forza-Motorsport-2-ReGPU, 0.14.0;
  the `FM2` folder is only an install). From it: **the Wine stereo mix** (src/audio/audio.cpp, FM2 2912e46/e45f333):
  under Wine on a 2-channel device, FAudio's 5.1 downmix puts every channel at ~1/3 level; the voice now gets the
  Windows matrix (fronts 1, centre/backs 0.707). The Deck reports 2 channels: active there (log "mixed to stereo
  (Wine)"); the game should be ~3x louder than before. `PGR3_NO_STEREO_MIX=1` = Wine's mix. Not taken: FM2's
  seamless_loops.h (a table from FM2's own banks), car_sound.cpp (FM2-specific); its codec-ahead pool is in PGR3.
  src/audio/xma.cpp, all live switches:
  - **Seek guard false positives fixed** (`SpillBits`): it read the packet header's top 6 bits as the frame count;
    walked as FFmpeg does (PGR4's ParsePacket: until a frame's last bit says none follows), the spill matches. Before,
    ~400 sequential packets a second ("restart: buffer 0 packet 2 after packet 1") got a fresh codec and dropped the
    decoded audio ahead (~16k samples/s). `PGR3_XMA_HEADER_FRAMES=1` = old.
  - **Seamless loops** (PGR4 ec98092's exact model, `kHwLead` 384): the codec carries on across a loop and the seam is
    cut from its output (Decoder::loopGaps, rawOut/fedFrames; loop end subframe = dword 1 bits 12-13, skip 17-19, as
    PGR4 found); the seek guard lets the loop start packet through (loopJump). `PGR3_XMA_LOOP_FLUSH=1` = old.
  - **Stream start trims** (`StartTrim`, adapted from PGR4's xma_trims.h, MIT): a stream's own start drops
    trim_start - 384 of the codec's output. ~60-100 starts a second trimmed in a race. `PGR3_XMA_NO_START_TRIM=1` = old.
  - Reset causes are now counted in the 5 s xma line (loop passes / seamless / starts trimmed / ends / cancels / re-inits).
  - Evidence (PGR3_AUDIO_CAPTURE_DIR captures, 45 s races; scratchpad loops.py): old: 69k packets decoded, 28k codec
    replacements, 22k guard resets, 13k drop-aheads, and a 4-packet engine loop gave **768 samples per pass** (the loop
    mostly thrown away); new: 42k packets, 0 replacements/resets/drops, every loop the same full length each pass
    (e.g. 109,824 samples every pass of a 21-packet loop). 0 decode errors, 0 FFmpeg complaints, no uncut seams.
  - **Speed** (3 interleaved pairs, 120 s uncapped): 84.7 -> 89.7 fps, 1% low 45.9 -> 50.5, 0.1% low 34.8 -> 39.5,
    frames over 16.7 ms 397 -> 202. (Codec reopens 2,600 -> 0 per 5 s.)
  - **Not yet heard: the user should listen to a race** (engine note, gear changes, crowd/ambience). If something
    sounds wrong, A/B with the three switches above (live: debug server `switch NAME 1`).
  - Not ported: PGR4's xma_lock register rotation (PGR3's log shows 0 lock polls: kernel API, quirk A2 doesn't apply),
    XMP notification/hang fixes (no such hangs in PGR3), stream-end exact drain (0 stream ends in a race).
- **More drains (user: after FM2, prod PGR3 for suspicious performance drains).** Profiled with Linux perf
  (`tools/deck/profile.sh <label> [s]`: the AI race under perf through ReGPU-V/work/tools/perf-host, names from the
  pdb, per-thread report in out/profile-<label>/report.txt; from the FM4/FM2 Deck work).
  - **The game spun on GPU fences:** D3D's block-until-fence (sub_823D0B48) polling sub_823DEE80 was ~25% of the main
    thread. `src/hooks/fence_wait.cpp`: a round that says "go on waiting" is followed by a 100 us sleep (Wine
    nanosleep, regpu/wine_wait.h). Uncapped race, 3 clean interleaved pairs: 88.2 -> 89.6-89.9 fps, lows equal or
    better; at a 60 cap the main thread 84% -> 50% of a core (total CPU about the same in that window).
    `PGR3_FENCE_SPIN=1` (live): spin as before.
  - **Import tracer checks** on every kernel import call (~2% of a worker): now an inline `g_traceImports` test
    (kernel/imports.h; PGR3_TRACE_IMPORTS still works).
  - Checked, fine: the audio mixer runs exactly 188 callbacks/s (no top-ups, nothing dropped); Wine ntdll ~4.6% of all
    samples; the command processor's register log (memmove + LogRegs ~36% of that thread) is off the critical path.
  - **Trap:** perf/llvm-symbolizer on a PDB name inlined code after its inline function, so `PPCCRRegister::compare`,
    `simde_mm_*`, `gpureg::Get` in a report are not calls (no S_GPROC32 in the pdb; forcing them inline changed nothing:
    tried in the FM2 work and reverted).
  - Result (uncapped AI race, 1280x800 1x): 89.7 fps, 1% low 54.5, 0.1% low 43.2, worst 25.8 ms; no crash.
- Left: the vsync-60 lows are mostly frames of 17-25 ms shown as 33 (GPU peaks ~21 ms/frame in heavy spots): a
  40 fps mode, adaptive vsync or dynamic resolution would address that. The D3D12 build has none of the Vulkan-only
  changes (fence splits etc. are ReGPU-V code; PGR4's D3D12 versions exist if the PC build wants them).
- Open items now: the launcher's renderer setting still needs a Windows build (no .NET on the Deck); headlight
  projection needs console night footage (see session 7).

## Start here (session 9)

**Session 8 (2026-11-26): committed and pushed everything; Vulkan made the main renderer (D3D12 a launcher switch);
then the user's report of brief visual glitches in moving races, investigation half done.**
- **Repos:** PGR3 main pushed (2886183 sessions 6-7, 7bd2b4f renderer switch, then this session's notes/tools). ReGPU-V
  branch `pgr3vk` pushed to github.com/braaap-stututu/ReGPU (8fb74d8 = the session 7 ReGPU changes); also kept as
  patches/regpu-vk/pgr3vk.patch (diff from fm4deck d5a5a65). The Deck has `gh` (~/.local/bin, logged in as
  braaap-stututu, git uses it): pushes work from here now.
- **Renderer switch:** see the session 8 bullet under "Start here (session 7)" below. The launcher wasn't compiled (no
  .NET SDK on the Deck): build it on Windows (launcher.cmd) and check Graphics > Display > Renderer.
- **The glitches (open, the user's top item):** in every lap, on every track, for a frame or a few (well under 0.1 s):
  (a) a building part **vanishes** (London ~39.9 s on the user's capture: the building at the right, lap 1);
  (b) a building face turns **blue/purple with a pink pattern** (London 46.8 s: a shop-front panel low on the
  left building, normal again at 46.9 s; looks like another texture's data, or a half-written one, for a frame);
  (c) **dark blotches** in the window bays of the Parliament facade (35.2 -> 35.3 s). The user's frames are their
  capture-card clips at 1280x720 with the timer on (the timer is the pgr3.log clock). Random in place, every lap.
  - Ruled out: **the texture write-watch** (REGPU_TEXTURE_WATCH_VERIFY=1 over a full 3-lap AI race: 0 "changed with
    no write seen"; out/logs/watch-verify.log). PGR3's NtReadFile calls GuestWillWrite before ReadFile, so streamed
    reads are seen.
  - Not reproduced in my own recording: tools/deck/race_scan.sh records every frame (debug server `shots 120 DIR raw`
    bursts) of a full AI race: 5120 frames, no magenta (tools/deck/magenta.sh: only the pink Puma boards), the
    Parliament stretch smooth. But recording halves the frame rate (26 fps), which may hide a timing race. The raw
    frames are in out/shots/scan-base (4.4 GB): delete them once this is settled.
  - **Two suspects left, both live switches:** (1) texture copies on helper threads (pgr3vk ab62f93, Deck session 6;
    D3D12 patch regpu-texture-copy-threads): the copy reads guest memory after the texture was hashed, so memory the
    game reuses for a newly streamed texture in between gives another texture's bytes or half-written ones ->
    purple/garbage, and zero alpha under the alpha test -> a vanished object. `REGPU_SYNC_TEXTURE_COPIES=1` = copy
    on the draw thread at hash time. (2) occlusion queries answering since session 7 (`REGPU_ZPD_STRICT_END=1` =
    the old rule): wrong "hidden" answers -> vanished objects.
  - **Next: the user records one AI race on the capture card and counts glitches per lap:** `tools/deck/race_ab.sh
    ab1` (default flips: lap 1 as now, at GO+62 s texture copies sync, at GO+124 s copies back + old query rule;
    flips with their timer readings in out/logs/ab1.flips.txt, log out/logs/ab1.log). GO comes at ~22.5 s on the
    timer, so lap 2 ~85-147 s, lap 3 ~147-210 s. Ask the user to start recording first, then launch.
  - If the copies are it: hash (or copy) at record time on the draw thread for textures in memory the title reuses,
    or keep the threads but read a snapshot; check the D3D12 patch has the same race. If the queries are it: compare
    REGPU_ZPD_SCALE and per-query answers (REGPU_TRACE_ZPD). If neither: next candidates are index/vertex caches
    (REGPU_INDEX_NO_WATCH, REGPU_NO_VERTEX_ARENA) and stale resolves (REGPU_NO_STALE_RESOLVES).
- **Deck gotcha (cost a run this session):** when the external display is DPMS-off, vsync presents run at 1 fps and the
  race route stalls before GO. The new scripts wake it first (`kscreen-doctor --dpms on`) and inhibit sleep while
  running (`kde-inhibit --power --screenSaver`).

## Start here (session 7)

**Session 7 (2026-11-25, 22:00-00:50): the user asked for an error check, a deep dive for physics/handling telemetry,
and whether VR and true triple screen can work as in FM3; then "keep working on it" for two hours.**
- **Session 8 (2026-11-26): Vulkan is the main renderer, Direct3D 12 a launcher switch** (the user's call). Each renderer
  is its own pgr3.exe build (the ReGPU is linked in; dxcompiler.dll differs), so the launcher picks the exe: settings.ini
  `renderer=vulkan|d3d12` (launcher-only key, default vulkan), Graphics > Display > Renderer. Vulkan: `pgr3.exe` (install),
  build/rel (Windows), build/deckvk (Deck); D3D12: `d3d12/pgr3.exe`, build/rel-d3d12, build/deck (Paths.ExeFor in
  launcher/Settings.cs; a missing build's button is greyed out; PGR3_EXE still overrides). play.cmd and tools/deck/play.sh
  follow the setting (PGR3_BUILD / PGR3_VK override). setup.ps1 now makes thirdparty/ReGPU-vk from fm4deck d5a5a65 +
  patches/regpu-vk/pgr3vk.patch when pgr3vk isn't on GitHub (before: silent fallback to D3D12). Both renderers share
  out/user's caches safely (pipelines.manifest is guest hashes; compiled caches have their own names). **The launcher
  wasn't compiled (no .NET SDK on the Deck): build it on Windows before trusting it.** Session 7's work committed
  (2886183), not pushed: no GitHub login on the Deck.
- **Next (the user decides):** (1) the user checks the sun glare in play facing the sun (`switch REGPU_ZPD_STRICT_END
  1/0`); (2) a headset test: vr=cockpit, then vr=stereo; (3) commits (nothing committed: PGR3 repo, ReGPU-V pgr3vk
  working tree, D3D12 patches 103-106); (4) launcher tabs for telemetry / triple / VR (PGR3's launcher has none;
  settings.ini keys work, the launcher keeps unknown keys); (5) the bright right-edge columns (triple seams).
- **Errors:** a 180 s AI race log (Vulkan) is clean: 0 XMA decode errors; only the known one-off boot imports
  (MmQueryStatistics, VdGetGraphicsAsicID, VdInitializeEDRAM, XNotifyPositionUI, Refresh,
  XamUserCreateAchievementEnumerator, kernel ordinal 1BF, XConfig 0003/0001). All tools/deck scripts pass `bash -n`.
- **Occlusion queries never ended in PGR3 (fixed, ReGPU-V gpu.cpp + D3D12 patch `regpu-zpd-a-counters-end`):** PGR3's
  D3D marks a query's end in the four A counters only (0xFFFFFEED; B stays 0, `REGPU_TRACE_ZPD`); ReGPU wanted A and B.
  Every query looked like a start: the game never got an answer (so `REGPU_ZPD_SCALE` "changed nothing", session 5),
  and the triple/stereo replays skipped every draw after the frame's first query. **Side effect to check in play:**
  the sun glare / flares now get real visibility answers (with FM3's inherited `REGPU_ZPD_SCALE=0.6`); not yet looked
  at against the console. Frozen-grid picture unchanged (mean diff 0.71/255 vs before); in a moving race nothing odd
  (no giant glare); no sun found on the frozen grid even looking round the sky with the VR fake pose (the glare is
  probably in the HDR composite, off while frozen): **A/B it facing the sun in live play** with `switch
  REGPU_ZPD_STRICT_END 1/0`. `REGPU_ZPD_STRICT_END=1` = the old behaviour.
- **Telemetry: done (src/hooks/telemetry.cpp).** settings.ini `telemetry=127.0.0.1:5300` (or `PGR3_TELEMETRY`),
  `telemetry_format=fm7|fh` (311 / 324 bytes), `telemetry_hz` (60). Forza "Data Out" layout (SimHub, motion rigs, dash
  apps). Checked with a UDP listener: 59.4 packets/s; motion, rpm, gear, inputs, wheels, suspension as expected; over a
  full 3-lap AI race: lap 0 -> 1 -> 2 -> 3, lap times 62.70 / 60.88 / 63.80 (best 60.88), position 6th -> 1st (and
  4th after a crash on lap 3, back to 1st), race distance to 8476 m (3 x 2754 + grid). Layout (car object, race
  object, per-car race blocks) in the file's header. Still 0: fuel, tyre temps, boost, power, torque, slip ratio.
- **Camera (how the scene is drawn):** the race camera object 0x828B3840 (camera update `sub_822FBC58`, skipped while
  the world is frozen/paused: +1208) holds the scene's camera matrix at +0x50 (rows right, up, forward, position) and
  the FOV at +0x40 (from `Cameras|Game FOV` 0x828B3710); the render function `sub_8229A548` reads it (as 0x828B3830 +
  16) into a stack view. The view hand-off `sub_82157EF8(view)` (renderer block 0x82545510..) is downstream: changes
  there reach only culling, LOD and motion blur. Frame order: `env PGR3_CAMERA_TRACE 40` (debug server).
- **Triple screen: working on both builds**, auto and physical layouts (physical 27"/70 cm/10 mm bezels:
  out/shots/triple-physical.png, 47.6 deg side turns, 141.5 deg across); (Vulkan: out/shots/triple-v2.png; D3D12 with patches 103-106:
  out/shots/triple-d3d12.png; its first triple boot compiles every vertex shader again, >10 min on the Deck). `PGR3_TRIPLE=1 PGR3_RESOLUTION=3840x720`
  (settings.ini triple=1, the triple_* layout keys as in FM3). Pieces: FM3's geometry (src/hooks/triple_screen.cpp)
  widening the race camera's own FOV (+0x40) after its update (any camera, replays too; first done through the Game
  FOV variable, which only the chase and in-car cameras read); motion blur + DOF off in triple/VR (0x8259CC94 / 0x8259CCB4); ReGPU
  `REGPU_TRIPLE_DEPTH=keep` (PGR3's reversed infinite projection), `REGPU_HUD_OVER=a7484f3b4c5ba1e1` (HUD on the centre
  screen only: PGR3 draws it over the composite), `REGPU_TRIPLE_SHARE_PASSES=1` (shadows and the car's cube map drawn
  once: 35 -> 42 fps at 3x720p on the Deck, 1979 draws per side screen). Seams continuous (out/shots/triple-hud.png).
  AI race at 3x 1280x720 on the Deck (Vulkan, 45 s): 51.8 fps avg, no errors (out/shots/triple-race.png).
  The thin light line at each seam is the frame's own right edge (the last 2 columns are brighter in the normal single
  screen too: out/shots/single-grid.png); not fixed. Narrowed with `ps a7484f3b4c5ba1e1 <expr>` live edits: the
  composite's scene fetch (slot 0) alone, its alpha, its UVs (0..1 exactly) and the vignette term are all clean at the
  edge, and nothing after the composite adds it; it comes from the composite's full math (the blend of those
  terms), cause not found: its own output (`ps ... float4(saturate(oC0.rgb), 1)`) already has it (46 vs 3 inside)
  while a hand-made TFetch2D(0, input.o[0]) at the same pixels is clean and the alpha is flat: next, compare the
  shader's own r5 after its first fetch (its predicated paths, `p`, run on the frozen grid too). FM3's REGPU_TRIPLE_FLIP / REGPU_VR_EYE_FLIP no longer set.
- **VR: head tracking works without a headset (fake pose); not tried in a headset.** OpenXR now builds in when
  thirdparty/openxr has include/ + bin/openxr_loader.dll (CMakeLists; set up on the Deck: headers linked from
  ReGPU-V/work/downloads-deck/openxr-sdk, loader from the FM3 release). `vr=cockpit|stereo`: the head's pose goes into
  the race camera's matrix after its update (vr_camera.cpp, a matrix not rebuilt since is restored first, so poses don't
  pile up while frozen), the headset FOV into +0x40. Checked with `PGR3_VR=cockpit PGR3_VR_ANY_CAMERA=1
  PGR3_VR_FAKE="30 0 0 0 0 0"` (turned left, FOV 108) and "0 20 15" (up, rolled): out/shots/vr-yaw30.png,
  vr-pitchroll.png. Without a runtime the log says "the OpenXR runtime has no Vulkan support" and the game goes on.
  In the race the cameras cycle with LB (in-car 0.76 m from the car's centre, two front views 1.51 / 1.68, chase 5.32 /
  6.45): head tracking applies within 2.5 m; checked in the in-car view (out/shots/vrc-incar-yaw30.png). Open: stereo can't be drawn without a
  headset (ReGPU needs poses); no HUD panel in stereo (REGPU_VR_HUD_UNDER is FM3's hash; PGR3's HUD is drawn over, so
  it shows in both eyes at screen depth). PGR3's in-car camera hardly moves against the car (yaw/pitch/roll within
  ~1 deg, sd ~0.1, measured over 30 s of AI race), so there is no shake or lean to switch off. vr=stereo also sets
  REGPU_TRIPLE_SHARE_PASSES (the second eye replays from the scene; untested without a headset).
- **ReGPU changes are uncommitted:** ReGPU-V (thirdparty/ReGPU-vk, branch pgr3vk): gpu.cpp (ZPD end, share passes,
  REGPU_TRACE_TRIPLE), hlsl.cpp (triple depth), renderer.cpp (HUD over). D3D12: patches 103-106 in tools/setup.ps1 and
  patches/README.md (`regpu-zpd-a-counters-end`, `regpu-triple-depth-keep`, `regpu-hud-over`,
  `regpu-triple-share-passes`); the D3D12 build compiles and its normal grid picture is unchanged
  (out/shots/d3d12-normal.png).

**Session 6, night (2026-11-25, 20:30-): the user asked about headlights, the translucent car, and the 1%/0.1% lows.**
- **Lows: XMA codec reuse (src/audio/xma.cpp, ported from the FM4/FM3 Deck work; same libavcodec 62.28.103).** Every
  audio stream restart (engine sounds, ~200 a second in a race) freed one FFmpeg XMA codec and opened another (3.6 MB
  private context, allocated and zeroed); retired codecs are now reset in place from a snapshot and go back to the pool.
  AI race on the Deck (Vulkan, 2+2 interleaved): **avg 77.2/78.1 -> 83.9/85.5 fps, 1% low 40.3/40.8 -> 48.5/51.2, 0.1% low
  26.5/31.4 -> 38.6/38.8, p99.9 29.7 -> 21-24 ms**, GPU 12.5 -> 11.5 ms (the CPU's heap work no longer takes the APU's
  power from the GPU). 0 decode errors and 0 FFmpeg complaints over 63,540 packets; FM4 checked the method bit-exact. Not
  listened to here: **listen to a race to confirm**. `PGR3_XMA_REUSE=0` = before. Both builds.
- **The Deck preset now holds 60.0 fps** in the race (cap 60, 240 Hz vblank, with codec reuse; vsync off because the
  screen was dark): 1% low 40-44, 0.1% low 32-34, ~1 frame over 2x the average per 40 s; the rest of the 1% low is frames
  that just miss a 16.7 ms slot and take one more vblank.
- Tried after that: CP idle sleep + `REGPU_HANDOFF_SPIN=0` uncapped (2+2): avg -0.7%, 0.1% low 34-35 -> 40-42, 1% low
  mixed: not made default (capped, the CP already sleeps; hand-off spin alone at cap 60: within noise). Per-thread CPU
  in the race (`tools/deck/threads.py`): the game's main thread 97% of a core (the limit now), draw thread 50%, command
  processor 31%, wineserver 13%. `PGR3_SAMPLE_THREAD` crashes under Wine (its own sampling thread, thread.cpp:742):
  use `PGR3_LONG_STACKS` instead.
- **Translucent car:** it stays see-through while the car is parked with the camera close behind (44 s tested) and is
  solid as soon as it drives. The game's near-camera car ghosting (`Car|Render|Ghost|Exterior min alpha distance`).
- **Headlights (not fixed; the game keeps them off, no emulation fault found):** the projected headlight code is in the
  shaders: `g_headlightEnabled` is bool[8] at b20 (pixel bools 148-155 = the night road shader's 8 projector branches,
  fetching `g_headlightMap` = t15), with matrices in pc180+. In every draw those bools are 0 and t15 (64x64 8888 @0D642000)
  is a constant dummy (FF 00 00 00); one projection matrix (pc184-186) is filled. The debug variable
  `Car|Lights|Headlights|On` (object 826486B0 = {vtable, value}, registered by 824CFA88 with default 0) is 0; poking it
  to 1 before the race (`PRE_RACE='C poke 826486B4 01000000' tools/deck/menus.sh race 3 1 x`) changes nothing (bools still
  0): the game gates them elsewhere. Car models' own lamps glow. Need console night footage to know if beams are expected.

**Session 6, evening (2026-11-25, 18:10-20:30): other cities and modes, Vulkan build. Nothing committed in the PGR3
repo; ReGPU-V commits on the local pgr3vk branch (bundle out/deck-push/regpu-pgr3vk.bundle, refreshed).** The user asked to save the notes,
work on the remaining issues and optimisation, then check the other modes/tracks. **Scripted menu routes beyond the
grid test were used for this (the user's request: "check the other modes/tracks"; CLAUDE.md otherwise forbids them):**
always on out/user-route restored from out/bench/baseline, never the user's profile. Helpers (scratchpad, worth moving
to tools/deck if kept): boot to the main menu, then PLAYTIME > Play on this Xbox 360 > Street Race > city > route >
options (LIGHTING left/right = day/night) > START > lobby (A opens "add opponent") > START RACE. Hold `trigger R` to drive.
- **Checked, all render correctly (Vulkan, 720p):** Tokyo day + night (neon, lit windows, tail lights, dusk sky), Las
  Vegas day, New York day, Nürburgring day, London night (street lamps, lit windows, Big Ben's clock face), the Playtime
  menus, lobby, race results screen ("SAVE REPLAY"), music in every race with the panel. No shader translation
  failures, no skipped draws in any of them.
- Also checked on Vulkan: Las Vegas night (Luxor beam, neon), the user's desktop settings 1440p + 4x MSAA + CMAA 2 (grid
  clean), FSR 1 and FSR 3 (3.1.6; switches off for one loading frame and back on). Texture write watch A/B in the race
  (REGPU_TEXTURE_NO_WATCH, 2+2): no difference; kept. Long race frames left (~10 per 40 s) are draw-thread time
  (WriteWatchArm/VirtualProtect, texture creation, a GPU wait), spread thin.
- **Deck 60 fps cap now holds (59.2 fps):** `tools/deck/play.sh`'s Deck defaults add `PGR3_VBLANK_HZ=240` (4x the cap): at
  the port's 2x (120 Hz) a frame that just misses its vblank waits 8.3 ms more. AI race at cap 60, 2+2: 56.1/57.4 ->
  59.2/59.2 fps, frames over 16.7 ms 1228/1200 -> 1099/1085, p99 25.9/25.7 -> 24.7/25.1 ms. Deck only (session 4 on the
  desktop measured 2x better at cap 60). Earlier "the governor lowers the clock" was only part of it.
  **Not measured with vsync on:** from ~20:00 the Deck's display was off (DPMS Off), and a FIFO swap chain on a dark
  screen presents at 1 Hz (benchmarks with PGR3_VSYNC=1 read 1.0 fps, GPU and draw thread idle: the environment, not
  the game; at 17:01 with the screen on, vsync uncapped gave 62 fps). The vblank change doesn't depend on vsync, but
  check `tools/deck/play.sh` with the screen on.
- Tried, no gain, left off: `REGPU_SPIRV_FINITE_MUL=1` (only straight-line programs; race 2+2 within noise),
  `REGPU_SPIRV_FETCH_BRANCHES=0` (per-shader times identical).
- **Final race numbers on the Deck (AI benchmark, 1280x800, uncapped, interleaved):** Vulkan 77.5/76.4 fps, 1% low
  39.0/34.8, 0.1% low 26.4/24.7; D3D12 76.7/76.7, 1% low 27.1/28.0, 0.1% low 18.2/19.6. Same average, Vulkan steadier.
  (The session started at ~70 fps with a 1% low of ~19 on both.)
- **Seen, the game's own:** a few seconds after GO, while the player's car hasn't moved, it turns translucent (a ghost)
  and the picture goes flatter, until it drives off. The game has its own car ghosting (`Car|Render|Ghost|...`:
  alpha by distance, "Display as ghost"), so this is very likely intended.
- **Fixed: saved photos were wrong** (Photo Mode > TAKE > SAVE, viewed in Gotham TV > Your saved photos): the saved
  JPG (`<user>\profiles\<profile>\content\00000001\profile.sav\<date>.jpg`) held a stale image (the ESRB notice screen),
  because the game JPEG-encodes the photo on the CPU from a 1280x720 8888 resolve that ReGPU kept on the GPU. Rule in
  src/gpu_host.cpp (resolveWriteback): 1280x720 8888 resolves not to the front buffer are written back **only in race
  frames** (the last front-buffer resolve was 1024 wide), since the menus resolve such images ~27 times a second. Checked:
  the JPG is the photo; the rule fired once (at TAKE), never in a 40 s benchmark race (74.4 fps, 1% low 35.7: unchanged).
  `PGR3_NO_PHOTO_WRITEBACK=1` = before. Found with PGR3_WRITEBACK (its log now names each new address, not only each size).
  After the fix the photo shows in Gotham TV's list preview and full screen, also after a restart (checked).
- Also checked: Cone Capture (London, cones, timer HUD), the pause menu, Photo Mode (framing, photo view, save dialogs).
- **Optimised (pgr3vk commit): direct SPIR-V for acyclic programs with backward jumps.** The slow shaders (vs DXC) took
  the translator's loop over all blocks because PGR3's compiler jumps back into a shared tail from out-of-line
  branches; RADV doesn't thread that loop. Blocks now run once in topological order when the jump graph is acyclic.
  576b4098 1.18 -> 1.00 ms/frame, 7a049592 and 5e7e7f87 -15% (two small shaders +5-10%); AI race 2+2: GPU 13.05 ->
  12.5-12.7 ms, 75.6 -> 76.9 fps; picture within boot noise; spirv-val clean. `REGPU_SPIRV_NO_DAG_ORDER=1` = before.
- Also checked: replays (save from the results screen with X, Gotham TV > Your saved replays > play: TV cameras,
  livery, crash overlay all right), Race Against the Clock (Tokyo, Aston Martin DB9, rolling start). Geometry Wars
  (gw.xex) stays out of scope (NOTES "on disc").
- **Unverified: night headlights.** No headlight pool on the road ahead at night. The game has projector lights
  (`g_headlightMap/Direction/Enabled` in shaders; the night road shader a60b3ce6a7a5ceea has 9 projector branches on
  bools 148-156 fetching t15), but those bools are 0 in every road draw mid-race, and t15 (8888 64x64 @0D642000) is bound
  to ~all draws (a lookup table, not a headlight map). Needs night console footage before calling it a bug.

**Session 6, later (2026-11-25 afternoon): PGR3 now runs on the Vulkan ReGPU (ReGPU-V), the default on the Deck.**
The user: "keep fixing things ... when you run out of immediate issues swap out the DX12 regpu for the regpu-v vulkan
one" (FM4 `deck`, ReGPU, FM3-ReGPU; newest branches).
- **ReGPU-V:** `thirdparty/ReGPU-vk`, a clone of github.com/braaap-stututu/ReGPU on the **local branch `pgr3vk`**:
  origin/fm4deck (d5a5a65: FM3 + FM4 Deck work) merged with origin/main (0d86ba5), plus three commits: the control-flow
  end-pair target (= D3D12 patch 101), tiny passes at 1:1 (= 102) and REGPU_FRAME_ROOM_HEIGHT (= regpu-frame-room).
  **Not pushed** (no GitHub login on the Deck): push `pgr3vk` from there, then it can become a submodule
  (`git submodule add -b pgr3vk https://github.com/braaap-stututu/ReGPU thirdparty/ReGPU-vk`). Of the D3D12 stack's
  102 patches 85 were already in ReGPU-V; not ported: pso-lock and dxc-tuning (D3D12-only) and ring-ahead
  (D3D12 ring creation); texture-copy-threads was ported later the same day (below).
- **To finish the swap on GitHub / the Windows PC:** `out/deck-push/regpu-pgr3vk.bundle` (the commits on top of
  origin/fm4deck d5a5a65). In a ReGPU clone: `git fetch <bundle> pgr3vk:pgr3vk` then `git push origin pgr3vk`. After
  that `tools/setup.ps1` clones thirdparty/ReGPU-vk (branch pgr3vk) when it is missing, and CMake builds it. Windows also
  needs a DirectXShaderCompiler release in `thirdparty/dxc/bin/x64` (dxc.exe, dxcompiler.dll, dxil.dll with SPIR-V: a
  junction to the FM3 port's `thirdparty/dxc` works); CMake then sets REGPU_DXC_EXE from it. A build folder configured
  before keeps its cached PGR3_REGPU_DIR: re-configure with `-DPGR3_REGPU_DIR=<project>/thirdparty/ReGPU-vk`.
- **Port side:** CMake `PGR3_REGPU_DIR` (default thirdparty/ReGPU-vk when present, else the D3D12 ReGPU); Vulkan:
  FidelityFX `_vk` DLLs, `thirdparty/dxc/bin/x64` (dxcompiler + dxil with SPIR-V output; on the Deck a link to the FM3
  Deck tree's) beside the exe, `PGR3_REGPU_VULKAN=1` for the source. `src/gpu_memory_log.cpp` uses
  `regpu::GetGpuMemory`; `src/log.cpp` a no-op `LogStart` (else ReGPU-V's log.cpp is linked in: duplicate Log symbols).
  Deck: `build/deckvk`, `tools/deck/play.sh` and `gridshot.sh` use it by default (`PGR3_VK=0`: the D3D12 build/deck).
  Every `REGPU_*` default gpu_host.cpp sets exists in ReGPU-V.
- **Checked on Vulkan:** boot, intro, menus, event select, loading, countdown, race grid, live race, pause menu, garage
  walk-around: all render; trees/plates (the ported fix) right; vs the clip as close as the D3D12 build or closer.
- **Speed on the Deck (720p, MSAA 1x, frozen grid, interleaved):** D3D12 (vkd3d-proton) 74-75 fps, GPU 12.7-12.8 ms;
  Vulkan 70-71 fps, GPU 13.5-13.8 ms (draw thread ~95% both). Per shader (`REGPU_GPU_SHADERS=1`): the gap is mostly
  four fetch-heavy blended shaders (5e7e7f87820d2491 0.22 -> 0.53 ms, fca3808b9c179973, 7a0495928b324ef4,
  f4ea385cb1277dbb) that ReGPU-V's **direct SPIR-V translator** makes slower than DXC does (through DXC,
  `REGPU_SPIRV_DIRECT=0`, they cost what D3D12's do) while the world shader 576b4098 is faster direct. All-DXC total
  13.08 ms vs direct 13.44 (warm caches), but DXC's cold boot is many minutes on the Deck: direct kept. New switch
  `REGPU_DUMP_SPV=<hash>` (pgr3vk commit) writes a shader's direct SPIR-V (base + variants) to <user>/shaders and logs
  the HLSL path's cache key (its DXC SPIR-V: shadercache/<key>.vk1.spv). For 5e7e7f87820d2491: the register file is a
  Function array indexed by constants only (no scratch); direct keeps values in local variables (DXC: SSA, phis); 33
  samples direct vs 153 in DXC's unspecialised base. Next: pair the drawn variants and compare RADV's register counts.
- **Music (QUIRKS A4) fixed:** `src/audio/xmp.cpp`, our own media player (FFmpeg avformat + wmav2/mp3 -> 48 kHz,
  256-frame big-endian float blocks to the game's XMPCaptureOutput callback; layouts as Xenia/PGR4). PGR3 reaches the
  player three ways: XMsgInProcessCall, **XMsgStartIORequest** (its playlists; was "function failed") and
  **XMsgSystemProcessCall** (play; was an unimplemented import). Menu music, the race soundtrack (e.g. "No Limit" by
  Daichi Miura) and the countdown/pause **"now playing" panel** as in the clip. Audio itself not heard (no listening
  here): data flows at 938 blocks / 5 s (48 kHz), no starvation. `PGR3_NO_XMP=1` old behaviour, `PGR3_TRACE_XMP=1`.
  PGR3 sets the player's volume to 0 at boot (XMPSetVolume) and takes the music with title render 1 (it plays it through
  its own voice): the player volume is not applied to title-rendered capture (applied, every block was silent). Checked
  by reading the capture blocks in a race: peaks 0.18-0.56, L/R correlation 0.79-1.00.
- **Deck race stutter halved (both renderers):** `tools/deck/bench.sh` (the AI benchmark race, 1280x800, MSAA 1x,
  uncapped) showed ~70 fps but a 1% low of ~19 fps, ~420 frames over 16.7 ms in 40 s, on Vulkan and D3D12 alike.
  Long-frame main-thread samples (`PGR3_LONG_STACKS=30 PGR3_LONG_STACKS_THREAD="guest 1 "`): (1) **NtReadFile logged
  every read over 4 MB with a symbolised host stack** (dbghelp, slow under Wine) on the reading thread: the race streams
  London_sunny.stream2 in 5-8 MB chunks from the main thread; now only with `PGR3_TRACE_LARGE_READS=1`; (2) VfsResolve
  checked `<user>\mods` on disk for every disc path (is_directory + listing, ~1 ms each under Wine): now read once a
  run. A/B (the log switch, 2+2 interleaved): avg 69.5 -> 72.5 fps, **1% low 19 -> 33-37 fps**, p99 46 -> 23 ms.
  Then (Vulkan) **texture copies on 3 helper threads** (pgr3vk commit, as the D3D12 stack's regpu-texture-copy-threads;
  `REGPU_SYNC_TEXTURE_COPIES=1` = before), 2+2: 1% low 30-33 -> 35-36, 0.1% low 15-19 -> 23-25 fps, p99.9 43-47 -> 37 ms,
  same picture. Left: ~10 long frames (26-36 ms) per 40 s.
- **Deck presets:** a 60 fps cap doesn't hold in the race (56 fps): capped, the Deck's governor runs the GPU at ~920 MHz
  against ~1105 uncapped (sensors during the run), and the frame no longer fits. Uncapped + vsync 62 fps. Lower
  resolution barely helps (GPU time is per draw: 1024x640 12.9 ms vs 13.9). 1280x800 renders 1280x720 with bars (views
  narrower than 16:9 aren't supported: Settings::RenderSize). (Fixed later: a 240 Hz guest vblank, below.)
- **The remaining tone difference is the capture, not the game:** the clip's static loading-screen UI (no lighting or
  exposure) shows the same levels curve as the race (ours 22 -> 13, 53 -> 48, 83 -> 84, 197 -> 211, 227 -> 240; black
  4 -> 0): video levels (the 360's reference level vs the capture card), an affine stretch ~1.1x - 14. Race with that
  curve applied: within +0.012..0.028 everywhere. Not to be "fixed" in the game. (Earlier "UI matches" was 2 points.)
- **Garage:** after the capture curve, shadows and mids match the clip (p25 22 vs 20-26, p50 41 vs 36-44); highlights
  hotter (p95 208 vs 97-134) but the clip moves the camera with motion blur and DOF and sees other walls: not
  comparable without the same view; left open.

**Session 6 (2026-11-25, on the Steam Deck; nothing committed or pushed):** the project was copied to the Deck
(`/home/deck/claude projects/PGR3ReGPU`). The user gave a capture-card clip of the console (London, pause -> restart ->
loading -> countdown -> GO; 12.5 s, 720p) and asked to make the build work on the Deck and fix the visual issues
against it.
- **Deck build works**: cross-built in the `regpu` distrobox (clang-cl + xwin), runs through Proton 11.0; how-to in
  `tools/deck/README.md` (build/deck, `tools/deck/fxc` stand-in for fxc, `play.sh`, `gridshot.sh`, `make_patch.sh`).
  The Windows exe from session 5 also runs under Proton as is.
- **Fixed: every tree in London missing (trunks too) and black licence plates** (true on Windows too: session 5's
  "grid matches the console" was before a reference with trees). New ReGPU patch **`regpu-cf-end-pair-target`**
  (101st; README row has the whole hunt): a jump target in the second half of the CF pair holding the program's end
  wasn't parsed on, so the tree shader's leaf branch stopped after its texture fetch with alpha 0 and alpha-to-mask
  discarded every pixel. Stack check: `tools/deck/make_patch.sh -` prints no diff over 101 patches. **The user's fork
  branch `pgr3` doesn't have it yet**, and setup.ps1's "all 100" text in this file is now 101.
- **Partly fixed (see "second round" below): the race picture was brighter than the console's** after the exposure settles (clip 11.5-12.4 s vs ours 15 s
  after releasing the grid freeze): road 42 vs 28, barrier 67 vs 53, building 107 vs 91, sky top R 129 vs 107: about
  `console = ours^1.2` everywhere. Not the capture path: flat UI greys on the loading screen match (icon 80 vs 80,
  white line 163 vs 162). Not the display ramp: ReGPU applies PGR3's table (0.5 -> 0.45), shots include it
  (REGPU_NO_GAMMA changes them). So it is the game's exposure/tone chain. Its passes (frame end, race): 54077a9a
  (log2 luminance x mask into 64x64) -> e81784d4 (16x16, 4x4) -> e9708945 (1x1) -> 22517658 (adaptation, 1x1 k_16_16_FLOAT
  ping-pong 0D102000/0D103000, clamped to pc[2].x..pc[2].y) -> 6088543b (bloom 341x200) -> def7b5ea -> composite
  **a7484f3b** (exposure = pc[0].x / (adapted + pc[3].x), bloom added). Next: read the adapted value live (ps edit with
  the '@anchor|statement' form) and check log2 of 0 (ReGPU gives -FLT_MAX), the 7e3 scene range and the bloom.
- Also seen: no "now playing" music panel in the countdown: music (WMA through XMP) isn't implemented (QUIRKS A4).
  "LOADING ..." missing in our two loading-screen shots: the console's text blinks (absent in 2 of 5 clip frames),
  not shown to be a bug.
- Garage (user's second console clip, "View your cars" -> MORE -> Walk around this garage, NSX GT2; driven on
  out/user-route through the debug server): geometry, textures, plates, wall pictures and camera motion blur match.
  Same exposure problem, stronger: ours mean brightness 65 vs the console's ~48 (35-62 over the clip), 95th percentile
  216 vs ~130, 3.7% clipped white; the window light patches on the beige back wall and floor blow out where the
  console keeps them dim. Fixing the exposure chain is the next item for both.
- **Exposure, second round ("keep fixing"):** new ReGPU patch **`regpu-tiny-pass-native-scale`** (102nd): surfaces of
  pitch <= 80 px (PGR3: only the luminance reduction 64->16->4->1, pitch 80) drawn at 1:1 instead of the output's
  scale. Live race, 720p, 2+2 interleaved vs the clip at 12.4 s (pre-ramp, per region): sky +0.050 -> +0.01, other
  surfaces +0.05 -> +0.03..0.04; highlights/upper mids now match (152->147, 168->169, 199->201). **Left: the darks**
  (our black ~10 vs the console's ~1, fading out by ~160). The garage did NOT change (mean 65.6 -> 66.2 vs the
  clip's ~48; the window light on the back wall still blows out): its brightness comes from elsewhere (showroom
  debug vars `Lighting|Show Room|Gamma`, `Gamma scale` suggest a path of its own).
- **Measuring lesson: the frozen grid can't judge exposure.** The composite a7484f3b applies exposure + bloom only
  under a predicate `p` that is 0 while time is frozen (checked by writing p to the output), so frozen-grid A/Bs of
  exposure, bloom or the luminance chain show nothing; earlier "native vs 720p" frozen differences were noise.
  Release the freeze (`env PGR3_FREEZE_TIME 0`), wait 15 s, shoot, compare to the clip at 12.4 s.
- Ruled out for the brightness: the capture path (UI greys match), the display ramp (shots include it; flat
  0.1/0.25/0.5 -> 12/48/115 at any resolution), fog (~0 near), bloom (removing it: <=0.006), sun glare / occlusion
  queries (`REGPU_ZPD_SCALE` 0..1 live: no change), native vs CPU DXT decode, the ambient cube (faces sane), the
  16x16 lighting table t11 @0ED2D000 (its bytes are real lighting data: zeroing them live turns buildings black;
  restored), the tree-shadow multiply pass d0fbdf6f (vertex strength FFFFFFFF, texture 0.42..0.93).
  `REGPU_NO_PACKED_CONSTANTS=1` gives translated shaders the game's real constant numbers (slow boot: every shader
  recompiles and the route times out; drive it by pressing A).
- Debugging lesson: the debug server's `ps HASH EXPR` replaces only the final return, **after** the alpha test and
  alpha-to-mask discard, so a "tint" proves nothing for alpha-tested draws. Read the HLSL first.

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
