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
