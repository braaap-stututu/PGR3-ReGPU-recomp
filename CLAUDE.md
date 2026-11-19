# PGR3 ReGPU: working rules

Static recompilation of Project Gotham Racing 3 (4D5307D1, Europe disc XEX built 2006-10-05; TU5 not
used) on ReXenon + ReGPU. **ReXenon is the only recompiler**: don't add hedge-dev XenonRecomp back. Templates: PGR4-ReGPU (`reference/PGR4-ReGPU`, upstream clone: engine,
runtime) and the FM3 ReGPU recomp (`G:\claude code projects\FM3-ReGPU recomp`: process, tools, patch
stacks). Plan: PLAN.md. Quirks: PGR3-QUIRKS.md.

## Session protocol
- Start every session by reading NOTES.md "Start here (session N)".
- At the end of every session, update NOTES.md without being asked: status, decisions, every patch and
  why, what is broken, and a session log entry. Bump "Start here".
- The user decides what comes next. Ask which open item to take when there's a choice.

## Addresses and code
- **Never copy PGR4/FM2/FM3/FM4 `sub_XXXXXXXX` addresses into PGR3.** Re-find every one (ppcdis, strings,
  xrefs) and record it in `src/hooks/addresses.h` once that exists.
- Addresses are in the disc XEX (`out/tu5/default_tu5.xex` is a copy of it with `-Base`).
- Don't run other repositories' code. Read it and write our own. `reference/` is read-only.
- Never commit game data: ISO, `game/`, `out/`, `dumps/`, the TU and DLC folders, extracted files.

## Patches
- Game-neutral GPU fixes go in ReGPU as a `patches/regpu-*.patch` behind a `REGPU_*` switch, listed in
  `tools/setup.ps1` and `patches/README.md`. PGR3-only rules go in the port's GPU host file.
- Make a new patch on top of the stack with a scratch index (`GIT_INDEX_FILE`, `git read-tree HEAD`,
  `git apply --cached` each patch, then diff). Then `tools/setup.ps1 -NoBuild`.
- **Never `git checkout` a file inside `thirdparty/ReGPU`**: that drops every patch's change to it.
- `*.patch` must stay LF (`.gitattributes`).

## Testing
- The user starts the game with `play.cmd` and drives to the test spot. Claude may run the automated
  grid-test boot itself (minimized), and must not script other boot-to-race routes.
- Ask the user to close the game before a rebuild. Restart it with CloseMainWindow, never by killing it
  mid-save.
- Measure numerically; "looks fixed" by eye isn't enough. A/B only with one change, interleaved runs.
- Saves: swap or restore only while the game is closed. Never touch the user's own profile in scripted runs.
- Downloads and installs need the user's approval first.

## Windows shell gotchas
- PowerShell 5.1 `>` writes UTF-16: redirect native tools through `cmd /c`.
- `core.longpaths=true` is set for this repo.
- Build with `tools\vcenv.cmd` (VS 2022 Build Tools + LLVM clang-cl).
