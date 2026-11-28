// Minimal fxc stand-in for Deck cross-builds: fxc /nologo /O3 /T <profile> /E <entry> /Fo <out> [/D NAME[=VALUE]]... <in>
// Compiles with d3dcompiler_47.dll (the same compiler fxc uses), run through Wine.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { LPCSTR name, def; } Macro; // (D3D_SHADER_MACRO)
typedef HRESULT(WINAPI* PFN)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, void**, void**);
typedef struct { void* vt; } Blob;
typedef struct { HRESULT(WINAPI* qi)(void*, const IID*, void**); ULONG(WINAPI* ar)(void*); ULONG(WINAPI* rl)(void*);
                 void*(WINAPI* ptr)(void*); SIZE_T(WINAPI* size)(void*); } BlobVt;
int main(int argc, char** argv) {
    const char *prof = 0, *entry = "main", *out = 0, *in = 0; UINT flags = 0;
    Macro macros[65] = { 0 }; int nm = 0;
    for (int i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "/T") && i + 1 < argc) prof = argv[++i];
        else if (!_stricmp(argv[i], "/E") && i + 1 < argc) entry = argv[++i];
        else if (!_stricmp(argv[i], "/Fo") && i + 1 < argc) out = argv[++i];
        else if (!_stricmp(argv[i], "/O3")) flags |= (1 << 15);   // D3DCOMPILE_OPTIMIZATION_LEVEL3
        else if (!_stricmp(argv[i], "/nologo")) {}
        else if (!_stricmp(argv[i], "/D") && i + 1 < argc && nm < 64) { // (FM2's SMAA presets)
            char* d = _strdup(argv[++i]); char* eq = strchr(d, '=');
            if (eq) *eq++ = 0;
            macros[nm].name = d; macros[nm].def = eq ? eq : "1"; nm++;
        }
        else if (argv[i][0] != '/') in = argv[i];
        else { fprintf(stderr, "fxc_shim: unknown option %s\n", argv[i]); return 2; }
    }
    if (!prof || !out || !in) { fprintf(stderr, "usage: fxc /T prof /E entry /Fo out in\n"); return 2; }
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    PFN compile = m ? (PFN)GetProcAddress(m, "D3DCompile") : 0;
    if (!compile) { fprintf(stderr, "fxc_shim: d3dcompiler_47.dll not found\n"); return 3; }
    FILE* f = fopen(in, "rb"); if (!f) { fprintf(stderr, "fxc_shim: can't open %s\n", in); return 4; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* src = malloc(n); fread(src, 1, n, f); fclose(f);
    Blob *code = 0, *err = 0;
    HRESULT hr = compile(src, n, in, nm ? macros : 0, 0, entry, prof, flags, 0, (void**)&code, (void**)&err);
    if (err) { BlobVt* v = err->vt; fwrite(v->ptr(err), 1, v->size(err), stderr); }
    if (FAILED(hr) || !code) { fprintf(stderr, "fxc_shim: compile failed 0x%08lx\n", hr); return 1; }
    BlobVt* v = code->vt; FILE* o = fopen(out, "wb"); if (!o) return 5;
    fwrite(v->ptr(code), 1, v->size(code), o); fclose(o);
    return 0;
}
