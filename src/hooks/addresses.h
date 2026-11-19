#pragma once
// PGR3 guest addresses the runtime hooks use, from PGR3's own disc default.xex (Europe, version 0xB, built
// 2006-10-05). Each is a function the recompiled image defines as sub_XXXXXXXX. Never FM2/FM3/FM4/PGR4 addresses.
// Checked with build/tools/ppcdis.exe.
//
// Lua 5.0.2 (the UI's scripting; PGR3-QUIRKS G3):
//   luaD_throw  8233D9D0: if L->errorJmp (L+0x58) { errorJmp->status = code; longjmp(&errorJmp->b, 1) }
//               else { G(L)->panic(L) (G = L+0x10, panic at +0x28); exit(EXIT_FAILURE) (8247C080) }.
//               An unprotected error ends the game through exit -> KeBugCheck.
//   setjmp 8247F3D0 / longjmp 8247F2B0 (config/pgr3.toml).
// lua_State: top at +0x08, stack at +0x1C (base for indexes), stack_last at +0x18, errorJmp at +0x58.
// TObject: 16 bytes, type tag (s32) at +0, value at +8 (a GCObject pointer in its first word).
// TString: 16-byte header (next, tt, marked, reserved, hash, len at +0x0C), then the characters.
#define PGR3_LUAD_THROW sub_8233D9D0
//
// Race, AI and game clock (session 3; src/hooks/ai_player.cpp, frame_time.cpp, grid_test.cpp):
//   car build     82178CB8 (car, entry): once per car as a race is built; entry+64 index, +68 type (0 player,
//                 1 opponent, 4 display car), copied to car+9128 / car+9124.
//   Lua AddBasicHuman 82354908 / AddBasicAI 82354C48 / AddRaceVars 82353E20 (DemoMode -> setup+2152) /
//   RaceStarted 823595F8 (GO: race+24157 = 1). Registration tables {name, func} at 820D52F0 and 820D60B0.
//   race setup 82799DF8 (game+20), race object 8279D6C0, game 82523778, app 82523814 (vtable 820C9E0C:
//   +8 update -> 8229B800(game), +12 render -> 8229B078(game)).
//   main 822A07F8 -> main loop 821B5B88. world update 82299690(game), once per frame in a race (game+13 set,
//   game+15 clear). game clock 82545774 ticked by 82158BC8 (+4 scale, +12 dt, +16 game time, +24 start, +28 re-base
//   flag; the same function ticks other clocks). Replay playback's fixed-step loop: 8237B400 / object 829460C0.
//   frame throttle: sub_821507A0 (frame function) blocks on the previous frame's fence through sub_823D0D50 (call at
//   821508DC, option byte 825445D8, fence saved at 82501D04); D3D block-on-fence 823D0B48 / hang check 823DEE80.
//   "update drivers" 8227E0F8 -> 8226EA20: double-buffered vertex-buffer lock (823D4888 -> 823D4498).
