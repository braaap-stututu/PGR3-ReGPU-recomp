// Lua 5.0.2 (PGR3's UI scripts): logs every error the VM throws, with its message, before the original
// luaD_throw runs. A protected call catches most of them (longjmp back to lua_pcall); one with no handler ends the
// game (panic, exit, KeBugCheck), so the log line is the only place its message shows.
// PGR3_LUA_QUIET=1: don't log caught errors (unprotected ones are always logged).
#include <cstdlib>
#include <string>

#include "hooks/addresses.h"
#include "kernel/imports.h"

PPC_FUNC_IMPL(__imp__sub_8233D9D0); // the recompiled original (C linkage)

namespace
{
// The value on top of the Lua stack, as text when it is a string (Lua 5.0: LUA_TSTRING = 4).
std::string TopMessage(uint32_t L)
{
    const uint32_t top = LoadBE32(L + 0x08);
    if (top < 0x10)
        return "<no stack>";
    const uint32_t obj = top - 16;
    const int32_t tag = int32_t(LoadBE32(obj));
    if (tag != 4)
        return "<type " + std::to_string(tag) + ">";
    const uint32_t ts = LoadBE32(obj + 8);
    const uint32_t len = LoadBE32(ts + 0x0C);
    return std::string(GuestPtr<const char>(ts + 16), len < 4096 ? len : 4096);
}
} // namespace

PPC_FUNC(PGR3_LUAD_THROW)
{
    const uint32_t L = ctx.r3.u32, code = ctx.r4.u32;
    const bool caught = LoadBE32(L + 0x58) != 0;
    static const bool quiet = getenv("PGR3_LUA_QUIET") != nullptr;
    if (!caught || !quiet)
        Log("lua: %s error %u (lr %08llX): %s", caught ? "caught" : "UNPROTECTED", code, ctx.lr, TopMessage(L).c_str());
    __imp__sub_8233D9D0(ctx, base);
}
