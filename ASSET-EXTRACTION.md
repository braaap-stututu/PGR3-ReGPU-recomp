# PGR3 asset extraction for a Godot renderer: research and plan

Goal: get every PGR3 asset (cars, tracks, objects, textures, materials, collision, racing lines, audio, movies, UI)
out of the disc into formats Godot 4 loads directly, with enough metadata to rebuild the look in a Godot renderer.

Rules that apply (CLAUDE.md): extracted output goes under `out\assets\` and is **never committed** (it is game data).
The extractor is our own code (we may read others' notes, not run their tools). Downloads (ffmpeg) need the user's OK.

## 1. What is on the disc (6.0 GB, research 2026-11-22)

| Folder | Size | Files | Contents |
|---|---|---|---|
| `Game/Areas` | 4.0 GB | 80 | Per city (London, Las_Vegas, New_York, Tokyo, Nurburgring) and 10 garages + RaceResults: `X_game.pak` (logic), `X_sunny/night.pak` (scene), `X_sunny/night.stream2` (streamed textures), `*_cubemaps0.xpr` |
| `Game/Cars` | 135 MB | 80 | One `.pak_hrd` per car (model + textures in one file) |
| `Game/Audio` | 1.2 GB | 558 | Per car: `.xwb` (XACT wave bank, big-endian `DNBW`) + `.dat` (`ONEX`/`SRTS` engine-sound object tree). `music/` 202 `.wma` + 7 `.mp3` |
| `Game/Objects` | 14 MB | 15 | `.gm3`: trophies, badges, cones (same container as cars) |
| `Game/Crowds` | 31 MB | 6 | `.pk2` (`ANIM` container, zlib): crowd animation |
| `Game/Cones` | 68 KB | 9 | `.sca` cone layouts (no visible header; maybe compressed or encrypted: to check) |
| `Game/CarTextures`, `Materials` | 4 MB | 7 | Plain `.dds`: car dirt/glass/scratch, environment maps, micro-texture palette |
| `UI` | 620 MB | 79 | `Movies/*.bik` (Bink 1, 41), `Textures` (`.xpr` XPR2, `.jpg`), `GameData` (`PGR3.ipx` car/colour list, `cardata.pak`, `cameras.pak`), `Text` |
| `GeometryWars` | 15 MB | 7 | The arcade mini-game |
| DLC `4D5307D1/00000002` | 75 MB | | 3 STFS packs, 31 cars (`tools/stfs.py` already opens STFS) |

### Container format (verified on cars, objects and London)

`.pak`, `.pak_hrd` and `.gm3` share one container: a 4-byte magic (`ENDI` for big-endian data, `ENSW` for swapped),
8 bytes, then chunks: `tag[4]`, `u32 LE`, then a `u32 LE` size with bit 31 set when the body is zlib-compressed. The
walker reaches the exact end of every file I tried, and every compressed chunk inflates. The payload inside is big-endian.

- **Car (`Atom.pak_hrd`)**: `SCNE` (1.5 MB, holds ~222 zlib sub-blocks: textures and vertex data) + `MISC` (2 KB of floats).
- **Object (`Cone0.gm3`)**: `SCNE` (zlib) + `TIME` (build timestamp).
- **Track scene (`London_sunny.pak`)**: `RT00..RT12` (13 route variants), `STRM` (index into `.stream2`, 356 entries),
  `SCNE` (10 MB), `SR00..SR12` + `PR00..PR12` (per-route scene/prop sets), `PVS` (visibility), `SHL` (5 MB, probably
  baked spherical-harmonic lighting), `TIME`, `NULL`.
- **Track logic (`London_game.pak`)**: per route `RTnn`, `CLnn` (collision, ~1.1 MB each), `RInn`/`AInn` (racing line /
  AI lines), `CVnn`, plus `GTME`.
- **`.stream2`**: a descriptor table (dimensions, formats), then zlib blocks each with a 16-byte header (sizes). The
  inflated blocks are raw **Xenos-tiled** DXT mip data (8 KB … 160 KB). So stream2 = track textures; geometry is in `.pak`.
- **`.xpr`**: standard XDK XPR2 (`TX2D`/`TXCM` records, named): documented, easy.
- **Scene data (`SCNE`) has no names or strings**: a custom binary graph (counts, offsets, floats). This is the
  part that needs real reverse-engineering.

### Prior art

- Cars only: a closed tool by id-daemon handled PGR3/PGR4 `.pak_hrd` (raw textures + a Noesis plugin, no vertex
  colours); its Xentax home is gone ([ResHax](https://reshax.com/topic/1092-project-gotham-racing-4-pak_hrd-stream2/),
  [archive page](https://sites.google.com/view/3d-model-archives/tools/daemon-tools/project-gotham-racing)). It proves
  the car format is tractable; we write our own.
- Nothing public for PGR3 tracks (`stream2`/scene `.pak`).
- **Our advantage: the game's own loader is in the recompiled code.** The chunk tags are strings in `.rdata` (`SCNE`
  at 0x82040D74, `MISC` 0x82040D6C, `ENDI` 0x82040D64, `STRM` 0x820AD1B0, `ENSW` 0x820AD1D0, `stream2` 0x820AD3B1),
  so their xrefs (`tools/pgr3_refs.py`) lead to the parsers. They are the exact spec, and we can run them live and
  read the structures they build in guest memory.
- **ReGPU already understands the GPU side**: Xenos texture formats, untiling and decoding (`texture.cpp`), vertex fetch
  formats, and dump switches (`REGPU_DUMP_TEXTURES`, `REGPU_DUMP_SHADERS`, `REGPU_DUMP_FRAME`…).

## 2. Approach

**Static extractor, using the game's loader as the spec and a ReGPU capture as the check.**

- *Static* (parse the files): gets everything — every car, every route, every LOD, collision and AI lines,
  including things the camera never sees. This is the deliverable.
- *Runtime capture* (record what ReGPU draws): only what one frame shows, with transforms baked into shader
  constants and no collision or lines. Too narrow as the main path, but it is ground truth: exact vertex layouts,
  which textures and shaders each mesh uses, the constants. We use it to check the static export, and as the source
  for materials.

Tools (all ours, in the repo; output in `out\assets\`):
- `tools/pgr3x/` (Python): container walker, chunk dump + JSON index, XPR2, STRM/stream2 index, glTF writer.
- `pgr3x.exe` (small C++ host tool built with the port's CMake): reuses ReGPU's Xenos texture untile/decode and
  vertex-format code, so textures and vertices match what the renderer does. Python calls it, or it does the heavy work.
- `REGPU_CAPTURE_SCENE` (new game-neutral ReGPU patch): on one frame, write every draw's vertex/index buffers + fetch
  constants, textures, shader hashes, constants and render state to JSON + blobs.

### Output for Godot

| Asset | Format | Notes |
|---|---|---|
| Meshes | glTF 2.0 binary (`.glb`) | One per car (with LODs, damage parts, wheels as nodes), one per track section/route. Original shader id, material constants and flags go in glTF `extras` for the Godot side. |
| Textures | DDS keeping BC1/BC3/BC5 (no recompression loss), PNG for uncompressed formats | Godot 4 imports DDS; PNG fallback if needed. Normal maps flagged. |
| Materials | `materials.json` per asset + glTF PBR approximations | The Xenos shaders can't be used as-is: record texture slots, constants and the shader hash. ReGPU's HLSL translation is the reference for writing Godot shaders (car paint, glass, road, lights). |
| Collision | Separate `.glb` (or JSON triangles) | Godot `ConcavePolygonShape3D`. |
| Racing/AI lines, routes, start grids | JSON (points, widths, tangents) | `Curve3D`/`Path3D` on the Godot side. |
| Lighting | `SHL` → JSON/EXR, cubemaps → DDS | For a baked-lighting look. |
| Audio | Ogg Vorbis | XWB: our own parser, XMA → decode needs ffmpeg (download, approval). WMA → Ogg via ffmpeg. |
| Movies | Ogg Theora (`.ogv`) | Godot's built-in video format. Bink 1 → ffmpeg. |
| Data | JSON | `PGR3.ipx` (cars, colours), `cardata.pak`, `cameras.pak`, engine-sound `.dat` trees. |

Layout: `out\assets\cars\<car>\{car.glb, textures\, materials.json}`, `out\assets\tracks\<city>_<sunny|night>\
{route_NN.glb, collision_NN.glb, lines_NN.json, textures\, lighting\}`, `out\assets\audio\`, `movies\`, `ui\`,
`data\`, plus `out\assets\manifest.json` listing everything.

## 3. Phases (each ends with a numeric check, like the optimisation work)

1. **Unpack everything** (short). Container walker for all `ENDI/ENSW/ANIM` files, stream2 block index, XPR2 reader,
   DLC through `stfs.py`. *Check:* every file walks to its exact end and every zlib block inflates (all 6 GB);
   chunk-tag statistics per file type written to `docs`. `.sca`: find out what it is.
2. **Find the loaders**. Xrefs of the tag strings → the `SCNE`, `STRM`, `SR/PR/CL/RI/AI` parsers. Record the
   addresses in `src/hooks/addresses.h`, struct layouts in `docs/formats/`. Optional debug hook: after a load,
   dump the structure the game built (pointer graph) to check our reading of the file.
3. **Textures** (first visible result). Texture headers in `SCNE`/`STRM` → Xenos format, size, tiling → `pgr3x`
   untile/decode → DDS/PNG. *Check:* compare with ReGPU's `REGPU_DUMP_TEXTURES` output for the same texture.
4. **Car meshes** (`.pak_hrd`, the smallest complete case). Vertex declarations (Xenos fetch formats: half floats,
   10:10:10 normals, colours), big-endian index buffers (lists/strips), node tree, LODs, wheels, damage parts →
   `.glb`. *Check:* `REGPU_CAPTURE_SCENE` in the garage/race: vertex/index bytes of the captured draws match ours.
5. **Materials**. Map each mesh's shader + texture slots + constants (from the capture) to a material record and a
   PBR approximation. Godot shaders for car paint/glass/road come from the ReGPU HLSL as reference.
6. **Tracks**. One city first (London sunny): `SCNE` + `SRnn/PRnn` geometry and instances, `STRM`→stream2 textures,
   then collision, lines, PVS, `SHL`, night variants, garages. *Check:* capture at the London grid matches; draw-count
   coverage (what fraction of the captured draws our export contains).
7. **Everything else**. Objects/trophies (`.gm3`), crowds (`.pk2`), cones, UI (`.xpr`, jpg), data tables; then audio
   and movies once ffmpeg is approved.
8. **Godot side**. `manifest.json` + an import script / `EditorImportPlugin` that builds scenes (materials, collision
   shapes, `Path3D` lines). Depends on how the user's renderer is organised.

Rough size: phases 1, 3 and 7 are mechanical. Phases 2, 4 and 6 are the real reverse-engineering (`SCNE` is one
format shared by cars, objects and tracks, so the work for 4 carries most of 6).

## 4. Questions for the user

1. Godot version, and is the renderer project somewhere I can read (to match its import conventions)?
2. First target: one car end to end (recommended: proves textures, meshes, materials), or textures for everything first?
3. Textures: keep BC-compressed DDS (exact, smaller), or PNG?
4. Materials: raw data (shader id, constants, slots) for your own shaders, or also PBR approximations?
5. Audio/movies in scope? They need ffmpeg (a download: needs your OK).
