using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace PGR3Launcher;

/// <summary>Where things are: the install (pgr3.exe, game\disc) and the user folder.</summary>
public static class Paths
{
    /// <summary>The install folder: PGR3_ROOT, else the first folder up from the launcher that holds build\rel\pgr3.exe
    /// (a source checkout) or pgr3.exe itself (an install).</summary>
    public static readonly string Root = Env("PGR3_ROOT") ?? FindRoot();
    public static string Exe => Env("PGR3_EXE") ?? (File.Exists(Path.Combine(Root, "pgr3.exe")) ? Path.Combine(Root, "pgr3.exe") : Path.Combine(Root, "build", "rel", "pgr3.exe"));
    /// <summary>The extracted Europe disc: game\disc in a source checkout (play.cmd's), else game\.</summary>
    public static string GameDirDefault => Directory.Exists(Path.Combine(Root, "game", "disc")) ? Path.Combine(Root, "game", "disc") : Path.Combine(Root, "game");
    /// <summary>PGR3_GAME_DIR, else the folder chosen in the launcher, else the default.</summary>
    public static string GameDir => Env("PGR3_GAME_DIR") ?? (GameSettings.Load().GameFolder is { Length: > 0 } g ? g : GameDirDefault);
    public static bool GameDirFromEnv => Env("PGR3_GAME_DIR") is not null;
    /// <summary>PGR3_USER_DIR (launcher.cmd sets out\user, as play.cmd does), else Documents\pgr3 as pgr3.exe picks it
    /// (src/user_dir.cpp).</summary>
    public static readonly string User = Env("PGR3_USER_DIR") is { } u ? Path.GetFullPath(u)
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "pgr3");
    public static string SettingsFile => Path.Combine(User, "settings.ini");
    /// <summary>Where pgr3.exe runs, so pgr3.log and the debug server's out\ folders land where play.cmd puts them.</summary>
    public static string RunDir => Path.Combine(Root, "out", "run");
    public static string Log => Path.Combine(RunDir, "pgr3.log");

    static string? Env(string name) => Environment.GetEnvironmentVariable(name) is { Length: > 0 } v ? v : null;

    static string FindRoot()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "build", "rel", "pgr3.exe")) || File.Exists(Path.Combine(dir.FullName, "pgr3.exe")))
                return dir.FullName;
        return Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
    }
}

/// <summary>
/// settings.ini in the user folder, the file pgr3.exe reads (src/settings.cpp). Keys the launcher doesn't know are kept
/// as they are.
/// </summary>
public sealed class GameSettings
{
    public string Profile { get; set; } = "Player";
    public string Resolution { get; set; } = "1280x720";
    public string Aspect { get; set; } = "auto";
    public bool Fullscreen { get; set; }
    public bool Vsync { get; set; } = true;
    public int MaxFps { get; set; } = 60;
    public int Msaa { get; set; } = 1;
    public string Aa { get; set; } = "off";
    public string Ssaa { get; set; } = "1";
    public bool Eqaa { get; set; }
    public string Gpu { get; set; } = "";             // gpu: the graphics card by name (ReGPU's REGPU_ADAPTER); "" = Windows' choice
    public string Upscaler { get; set; } = "off";
    public int Upscale { get; set; } = 1;              // upscale_quality: 0 native .. 4 ultra performance
    public int Sharpening { get; set; } = 50;          // percent; settings.ini holds 0-1
    public int Language { get; set; } = 1;
    public string PerfPreset { get; set; } = "off";    // perf_preset: ReGPU's Hyper presets (src/gpu_host.cpp)
    public static readonly string[] PerfPresets = ["off", "mild", "low", "extreme", "potato", "auto"];
    public bool FpsPanel { get; set; }                 // fps_panel: fps counter and frame-time graph (read by the game)
    public bool PerfLogLight { get; set; }             // perf_log_light: REGPU_PERF_LITE, one line a second in pgr3.log
    public bool AnisoFromGame { get; set; }            // aniso_from_game: each texture's own anisotropy instead of 16x
    public bool DxcShaders { get; set; }               // shader_compiler=dxc
    public bool VramUploadRing { get; set; }           // vram_upload_ring: ReGPU's upload ring always in VRAM; off = chosen by measuring (session 5)
    public bool AsyncPresent { get; set; } = true;     // async_present
    // Launcher only, set on the game's environment at launch (Game.Launch):
    public bool Anisotropy { get; set; } = true;       // off: REGPU_NO_ANISO
    public bool HideShaderOverlay { get; set; }        // REGPU_NO_OVERLAY
    public bool Timer { get; set; } = true;            // the test timer (off: PGR3_NO_TIMER)
    public bool PerfLog { get; set; }                  // REGPU_PERF: frame timings in pgr3.log (costs the draw thread)
    public bool DrawWorkers { get; set; }              // PGR3_DRAW_THREAD_PIECES (slower here: session 4)
    public bool DebugServer { get; set; } = true;      // REGPU_DEBUG on port 7342 (tools\pgr3ctl.ps1)
    public string GameFolder { get; set; } = "";       // launcher only: empty = Paths.GameDirDefault

    static readonly string[] Known = ["profile", "gpu", "resolution", "aspect", "fullscreen", "vsync", "max_fps", "msaa", "aa", "ssaa", "eqaa",
        "upscaler", "upscale_quality", "sharpness", "language", "perf_preset", "fps_panel", "perf_log_light", "aniso_from_game",
        "shader_compiler", "vram_upload_ring", "async_present", "anisotropy", "hide_shader_overlay", "timer", "perf_log", "draw_workers",
        "debug_server", "game_folder"];

    public static GameSettings Load()
    {
        var s = new GameSettings();
        foreach (var (k, v) in Read())
            switch (k)
            {
                case "profile": s.Profile = v; break;
                case "resolution": s.Resolution = v; break;
                case "aspect": s.Aspect = v; break;
                case "fullscreen": s.Fullscreen = v == "1"; break;
                case "vsync": s.Vsync = v == "1"; break;
                case "max_fps": s.MaxFps = int.TryParse(v, out var f) ? f : 60; break;
                case "msaa": s.Msaa = int.TryParse(v, out var m) ? m : 1; break;
                case "aa": s.Aa = Options.Aa.Any(x => x.Value == v) ? v : "off"; break;
                case "ssaa": s.Ssaa = Options.Ssaa.Any(x => x.Value == v) ? v : "1"; break;
                case "eqaa": s.Eqaa = v == "1"; break;
                case "gpu": s.Gpu = v; break;
                case "upscaler": s.Upscaler = Options.Upscalers.Any(u => u.Value == v) ? v : "off"; break;
                case "upscale_quality": s.Upscale = int.TryParse(v, out var q) ? Math.Clamp(q, 0, 4) : 1; break;
                case "sharpness":
                    s.Sharpening = double.TryParse(v, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var sh)
                        ? (int)Math.Round(sh * 100) : 50;
                    break;
                case "language": s.Language = int.TryParse(v, out var l) && l > 0 ? l : 1; break;
                case "perf_preset": s.PerfPreset = PerfPresets.Contains(v) ? v : "off"; break;
                case "fps_panel": s.FpsPanel = v == "1"; break;
                case "perf_log_light": s.PerfLogLight = v == "1"; break;
                case "aniso_from_game": s.AnisoFromGame = v == "1"; break;
                case "shader_compiler": s.DxcShaders = v == "dxc"; break;
                case "vram_upload_ring": s.VramUploadRing = v == "1"; break;
                case "async_present": s.AsyncPresent = v != "0"; break;
                case "anisotropy": s.Anisotropy = v != "0"; break;
                case "hide_shader_overlay": s.HideShaderOverlay = v == "1"; break;
                case "timer": s.Timer = v != "0"; break;
                case "perf_log": s.PerfLog = v == "1"; break;
                case "draw_workers": s.DrawWorkers = v == "1"; break;
                case "debug_server": s.DebugServer = v != "0"; break;
                case "game_folder": s.GameFolder = v; break;
            }
        return s;
    }

    public void Save()
    {
        var keep = Read().Where(kv => !Known.Contains(kv.Key)).ToList();
        var profile = new string(Profile.Trim().Where(c => !char.IsControl(c) && c != '=').Take(15).ToArray());
        var sharp = (Math.Clamp(Sharpening, 0, 100) / 100.0).ToString(System.Globalization.CultureInfo.InvariantCulture);
        var sb = new StringBuilder("# Project Gotham Racing 3 (pgr3) settings; the launcher edits this file.\n");
        sb.Append($"profile={(profile.Length > 0 ? profile : "Player")}\n");
        sb.Append($"resolution={Resolution}\naspect={Aspect}\nfullscreen={B(Fullscreen)}\nvsync={B(Vsync)}\nmax_fps={Math.Clamp(MaxFps, 30, 360)}\n");
        sb.Append($"msaa={(Options.Msaa.Any(x => x.Value == Msaa) ? Msaa : 1)}\n");
        sb.Append("aa=" + (Options.Aa.Any(x => x.Value == Aa) ? Aa : "off") + "\nssaa=" + (Options.Ssaa.Any(x => x.Value == Ssaa) ? Ssaa : "1") + "\neqaa=" + B(Eqaa) + "\n");
        sb.Append($"gpu={new string(Gpu.Where(c => !char.IsControl(c) && c != '=').ToArray()).Trim()}\n");
        sb.Append($"upscaler={(Options.Upscalers.Any(u => u.Value == Upscaler) ? Upscaler : "off")}\nupscale_quality={Math.Clamp(Upscale, 0, 4)}\nsharpness={sharp}\n");
        sb.Append($"language={Language}\n");
        sb.Append($"perf_preset={(PerfPresets.Contains(PerfPreset) ? PerfPreset : "off")}\n");
        sb.Append($"fps_panel={B(FpsPanel)}\nperf_log_light={B(PerfLogLight)}\naniso_from_game={B(AnisoFromGame)}\nvram_upload_ring={B(VramUploadRing)}\nasync_present={B(AsyncPresent)}\n");
        sb.Append(DxcShaders ? "shader_compiler=dxc\n" : "shader_compiler=fxc\n");
        sb.Append($"anisotropy={B(Anisotropy)}\nhide_shader_overlay={B(HideShaderOverlay)}\ntimer={B(Timer)}\nperf_log={B(PerfLog)}\ndraw_workers={B(DrawWorkers)}\ndebug_server={B(DebugServer)}\n");
        sb.Append($"game_folder={new string(GameFolder.Where(c => !char.IsControl(c)).ToArray()).Trim()}\n");
        foreach (var (k, v) in keep) sb.Append($"{k}={v}\n");
        Directory.CreateDirectory(Paths.User);
        File.WriteAllText(Paths.SettingsFile, sb.ToString());
    }

    static string B(bool b) => b ? "1" : "0";

    static List<KeyValuePair<string, string>> Read()
    {
        var list = new List<KeyValuePair<string, string>>();
        if (!File.Exists(Paths.SettingsFile)) return list;
        foreach (var raw in File.ReadAllLines(Paths.SettingsFile))
        {
            var line = raw.Trim();
            int eq = line.IndexOf('=');
            if (line.StartsWith('#') || line.StartsWith(';') || eq <= 0) continue;
            list.Add(new(line[..eq].Trim(), line[(eq + 1)..].Trim()));
        }
        return list;
    }
}

/// <summary>Choices the pages offer.</summary>
public static class Options
{
    // The console's language IDs (XGetLanguage). Only English has been played on PGR3 so far.
    public static readonly (int Value, string Name)[] Languages = [(1, "English"), (3, "Deutsch"), (4, "Français"), (5, "Español"), (6, "Italiano")];
    public static readonly int[] Fps = [30, 60, 120, 144, 165, 240, 360];
    public static readonly (string Value, string Name)[] Aspects = [("auto", "Auto (match resolution)"), ("16:9", "16:9")];
    public static readonly (int Value, string Name)[] Msaa = [(1, "Off"), (2, "2x MSAA"), (4, "4x MSAA")];
    public static readonly (string Value, string Name)[] Aa = [("off", "Off"), ("fxaa", "FXAA (fastest, softest)"), ("smaa", "SMAA (sharp)"), ("cmaa2", "CMAA 2 (sharpest, keeps fine detail)"), ("taa", "TAA (smoothest; can ghost in motion)")];
    public static readonly (string Value, string Name)[] Ssaa = [("1", "Off"), ("1.5", "1.5x (2.25x the pixels)"), ("2", "2x (4x the pixels)")];
    public static readonly (string Value, string Name)[] Upscalers = [("off", "Off (native)"), ("fsr1", "FSR 1 (any graphics card)"), ("fsr3", "FSR 3 (any graphics card)"), ("fsr4", "FSR 4 (RX 9000; else FSR 3)"), ("bilinear", "Bilinear (fastest, softer)"), ("xess", "XeSS (Intel; any recent graphics card)")];
    public static readonly (int Value, string Name)[] Upscale = [(0, "Native (anti-aliasing only)"), (1, "Quality"), (2, "Balanced"), (3, "Performance"), (4, "Ultra performance")];
    public static readonly (int Value, string Name)[] Sharpening = [(0, "Off"), (25, "Low"), (50, "Medium"), (75, "High"), (100, "Maximum")];

    // ---- Graphics cards (DXGI): the game picks one by a part of its name (REGPU_ADAPTER; settings.ini gpu=).
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DXGI_ADAPTER_DESC1
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Description;
        public uint VendorId, DeviceId, SubSysId, Revision;
        public nuint DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory;
        public uint LuidLow;
        public int LuidHigh;
        public uint Flags;
    }
    [ComImport, Guid("29038f61-3839-4626-91fd-086879011a05"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IDXGIAdapter1
    {
        void SetPrivateData(); void SetPrivateDataInterface(); void GetPrivateData(); void GetParent();
        void EnumOutputs(); void GetDesc(); void CheckInterfaceSupport();
        [PreserveSig] int GetDesc1(out DXGI_ADAPTER_DESC1 desc);
    }
    [ComImport, Guid("770aae78-f26f-4dba-a829-253c83d1b387"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IDXGIFactory1
    {
        void SetPrivateData(); void SetPrivateDataInterface(); void GetPrivateData(); void GetParent();
        void EnumAdapters(); void MakeWindowAssociation(); void GetWindowAssociation(); void CreateSwapChain(); void CreateSoftwareAdapter();
        [PreserveSig] int EnumAdapters1(uint index, out IDXGIAdapter1 adapter);
    }
    [DllImport("dxgi.dll")]
    static extern int CreateDXGIFactory1(ref Guid riid, [MarshalAs(UnmanagedType.Interface)] out IDXGIFactory1 factory);

    public sealed record Gpu(string Name, long MemoryMb);

    public static List<Gpu> Gpus()
    {
        var list = new List<Gpu>();
        try
        {
            var iid = typeof(IDXGIFactory1).GUID;
            if (CreateDXGIFactory1(ref iid, out var factory) != 0) return list;
            for (uint i = 0; factory.EnumAdapters1(i, out var adapter) == 0; i++)
            {
                if (adapter.GetDesc1(out var d) == 0 && (d.Flags & 2 /* software */) == 0 && list.All(g => g.Name != d.Description.Trim()))
                    list.Add(new Gpu(d.Description.Trim(), (long)(d.DedicatedVideoMemory >> 20)));
                Marshal.ReleaseComObject(adapter);
            }
            Marshal.ReleaseComObject(factory);
        }
        catch { }
        return list;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DEVMODE
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumDisplaySettingsW(string? device, int mode, ref DEVMODE dm);

    /// <summary>The primary display's modes from 1280x720 up, smallest first.</summary>
    public static List<string> Resolutions()
    {
        var set = new HashSet<(int, int)> { (1280, 720) };
        var dm = new DEVMODE { dmSize = (short)Marshal.SizeOf<DEVMODE>() };
        for (int i = 0; EnumDisplaySettingsW(null, i, ref dm); i++)
            if (dm.dmPelsWidth >= 1280 && dm.dmPelsHeight >= 720) set.Add((dm.dmPelsWidth, dm.dmPelsHeight));
        return set.OrderBy(m => (long)m.Item1 * m.Item2).Select(m => $"{m.Item1}x{m.Item2}").ToList();
    }
}

/// <summary>The game folder's default.xex against the one this port is built from: Project Gotham Racing 3 (Europe),
/// default.xex version 0xB (NOTES.md "Game files").</summary>
public static class Xex
{
    public const string Sha256 = "26FB114109F0FA997497C6836385B8AF391DB253D75C08D78D12F4899475A78A";
    static readonly System.Collections.Concurrent.ConcurrentDictionary<string, string> Answers = new();

    static string? Key(string xex)
    {
        try { var fi = new FileInfo(xex); return fi.Exists ? $"{fi.FullName}|{fi.Length}|{fi.LastWriteTimeUtc.Ticks}" : null; }
        catch { return null; }
    }

    static bool Matches(string xex)
    {
        using var f = File.OpenRead(xex);
        var ok = string.Equals(Convert.ToHexString(SHA256.HashData(f)), Sha256, StringComparison.OrdinalIgnoreCase);
        if (Key(xex) is { } key) Answers[key] = ok ? "ok" : "mismatch";
        return ok;
    }

    /// <summary>"ok", "mismatch", "unchecked" or "missing". Reads nothing but file info.</summary>
    public static string Status(string gameDir)
    {
        var key = Key(Path.Combine(gameDir, "default.xex"));
        if (key is null) return "missing";
        return Answers.TryGetValue(key, out var r) ? r : "unchecked";
    }

    public static string CheckNow(string gameDir)
    {
        var xex = Path.Combine(gameDir, "default.xex");
        if (!File.Exists(xex)) return "missing";
        Matches(xex);
        return Status(gameDir);
    }

    public static void Require(string folder)
    {
        var xex = Path.Combine(folder, "default.xex");
        if (!File.Exists(xex)) throw new InvalidDataException("That folder has no default.xex: pick the folder with the extracted disc's files.");
        if (!Matches(xex)) throw new InvalidDataException("That folder's default.xex isn't the one from Project Gotham Racing 3 (Europe).");
    }
}

/// <summary>Starting the game, as play.cmd does.</summary>
public static class Game
{
    public static bool Running => Process.GetProcessesByName("pgr3").Any(p =>
    {
        try { return string.Equals(p.MainModule?.FileName, Paths.Exe, StringComparison.OrdinalIgnoreCase); }
        catch { return true; }
    });

    /// <summary>Starts pgr3.exe; `extra` adds environment variables for this run (the benchmark's). The caller disposes it.</summary>
    public static Process? Launch(IReadOnlyDictionary<string, string>? extra = null, string? userDir = null)
    {
        if (!File.Exists(Paths.Exe)) throw new InvalidOperationException($"pgr3.exe isn't at {Paths.Exe}.");
        if (!File.Exists(Path.Combine(Paths.GameDir, "default.xex"))) throw new InvalidOperationException("No game files yet: choose the game folder (the extracted disc) on the Play tab.");
        if (Running) throw new InvalidOperationException("The game is already running.");
        Directory.CreateDirectory(Paths.RunDir);
        // pgr3.exe overwrites pgr3.log: keep the last run's as pgr3.prev.log.
        try { if (File.Exists(Paths.Log)) File.Copy(Paths.Log, Path.ChangeExtension(Paths.Log, ".prev.log"), true); } catch (IOException) { }
        var psi = new ProcessStartInfo(Paths.Exe) { WorkingDirectory = Paths.RunDir, UseShellExecute = false, CreateNoWindow = true };
        psi.ArgumentList.Add(Paths.GameDir);
        psi.Environment["PGR3_USER_DIR"] = userDir ?? Paths.User;
        var settings = GameSettings.Load();
        psi.Environment.Remove("PGR3_PROFILE");
        if (!settings.Anisotropy) psi.Environment["REGPU_NO_ANISO"] = "1";
        if (settings.HideShaderOverlay) psi.Environment["REGPU_NO_OVERLAY"] = "1";
        if (!settings.Timer) psi.Environment["PGR3_NO_TIMER"] = "1";
        if (settings.PerfLog) psi.Environment["REGPU_PERF"] = "1";
        if (settings.DrawWorkers) psi.Environment["PGR3_DRAW_THREAD_PIECES"] = "1";
        // The live debug server (127.0.0.1 only, tools\pgr3ctl.ps1); 7341 is left to FM3 builds.
        if (settings.DebugServer)
        {
            psi.Environment["REGPU_DEBUG"] = "1";
            psi.Environment["REGPU_DEBUG_PORT"] = "7342";
        }
        if (extra is not null)
            foreach (var (k, v) in extra) psi.Environment[k] = v;
        return Process.Start(psi);
    }
}

/// <summary>The shader and pipeline caches in the user folder.</summary>
public static class ShaderCache
{
    public sealed record Info(bool Exists, int Shaders, long Bytes, DateTime? Built);

    static readonly string[] Dirs = ["shadercache", "shaders"];
    static readonly string[] Files = ["pipelines.bin", "pipelines.manifest", "static_shaders.bin"];

    public static Info Get()
    {
        long bytes = 0;
        int count = 0;
        DateTime? built = null;
        foreach (var d in Dirs.Select(d => Path.Combine(Paths.User, d)).Where(Directory.Exists))
            foreach (var f in new DirectoryInfo(d).EnumerateFiles("*", SearchOption.AllDirectories))
            {
                bytes += f.Length;
                if (f.Extension is ".dxbc" or ".dxil" or ".ucode") count++;
            }
        foreach (var f in Files.Select(f => new FileInfo(Path.Combine(Paths.User, f))).Where(f => f.Exists))
        {
            bytes += f.Length;
            if (f.Name is "pipelines.bin" or "pipelines.manifest") built = f.LastWriteTime;
        }
        return new Info(bytes > 0, count, bytes, built);
    }

    public static void Delete()
    {
        if (Game.Running) throw new InvalidOperationException("Close the game first: it is using the shader cache.");
        foreach (var d in Dirs.Select(d => Path.Combine(Paths.User, d)).Where(Directory.Exists)) Directory.Delete(d, true);
        foreach (var f in Files.Select(f => Path.Combine(Paths.User, f)).Where(File.Exists)) File.Delete(f);
    }
}
