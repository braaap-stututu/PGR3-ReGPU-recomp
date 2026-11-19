// xexpatch <base.xex> <update.xexp> <out.xex>
// Applies a title update's delta patch (XenonUtils' XexPatcher, the code XenonRecomp uses) and writes the
// patched XEX, so xexdump / XenonAnalyse / the config scans all see the same image the recompiler does.
#include <cstdio>
#include <xex_patcher.h>

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: xexpatch <base.xex> <update.xexp> <out.xex>\n");
        return 1;
    }
    const auto result = XexPatcher::apply(argv[1], argv[2], argv[3]);
    if (result != XexPatcher::Result::Success)
    {
        std::fprintf(stderr, "xexpatch: failed (XexPatcher::Result %d)\n", static_cast<int>(result));
        return 1;
    }
    std::printf("wrote %s\n", argv[3]);
    return 0;
}
