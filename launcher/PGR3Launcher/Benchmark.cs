using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace PGR3Launcher;

/// <summary>
/// The benchmark race (PGR3 session 3): the race AI drives the player's car through the route profile's first race
/// (London, 6 cars, 3 laps) and the light performance log measures. pgr3.exe does it by itself with
/// PGR3_BENCHMARK=&lt;seconds&gt; (src/hooks/grid_test.cpp: the grid route without the freeze, a "benchmark: measuring
/// N s" line 10 s after GO, "benchmark: done" and the window closed after N s), as tools\benchmark.ps1 does.
/// It never runs on the player's save: it uses out\user-route with its profile put back from out\bench\baseline first
/// (a finished race would move that career on), and your settings.ini copied in for the run, so the result is for your
/// graphics settings. Needs a source checkout that has made the route profile. Results in the user folder's bench\.
/// </summary>
public static partial class Benchmark
{
    public static string Dir => Path.Combine(Paths.User, "bench");
    static string RouteUser => Path.Combine(Paths.Root, "out", "user-route");
    static string Baseline => Path.Combine(Paths.Root, "out", "bench", "baseline");
    public static bool Available => Directory.Exists(Baseline) && Directory.Exists(RouteUser);

    public sealed record Result(string Time, string Resolution, int Msaa, int MaxFps, string Options, int Seconds,
        double FpsAvg, double FpsLowestSecond, double P99Ms, double WorstMs, int LongFrames, int Draws, double GpuMs, double DrawThreadBusy);

    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public static async Task<Result> Run(int seconds, Action<string> progress)
    {
        seconds = Math.Clamp(seconds, 20, 600);
        if (Game.Running) throw new InvalidOperationException("Close the game first: the benchmark starts it by itself.");
        if (!Available) throw new InvalidOperationException("The benchmark needs the route profile (out\\bench\\baseline and out\\user-route, made by tools\\benchmark.ps1 in a source checkout).");
        // The route profile back as it was, and this player's settings for the run.
        var profiles = Path.Combine(RouteUser, "profiles");
        if (Directory.Exists(profiles)) Directory.Delete(profiles, true);
        CopyDir(Baseline, profiles);
        var routeIni = Path.Combine(RouteUser, "settings.ini");
        var savedIni = File.Exists(routeIni) ? File.ReadAllText(routeIni) : null;
        if (File.Exists(Paths.SettingsFile)) File.Copy(Paths.SettingsFile, routeIni, true);
        try
        {
            progress("Starting the game…");
            using var game = Game.Launch(new Dictionary<string, string>
            {
                ["PGR3_BENCHMARK"] = seconds.ToString(CultureInfo.InvariantCulture),
                ["PGR3_BENCHMARK_SETTLE"] = "10",
            }, RouteUser) ?? throw new InvalidOperationException("The game didn't start.");
            // Boot and menus ~15 s, the countdown, 10 s settling, the window, closing: give up well after that.
            var limit = TimeSpan.FromSeconds(seconds + 180);
            var watch = Stopwatch.StartNew();
            while (!game.HasExited)
            {
                if (watch.Elapsed > limit)
                {
                    try { game.CloseMainWindow(); if (!game.WaitForExit(15000)) game.Kill(); } catch (InvalidOperationException) { }
                    throw new TimeoutException("The benchmark race didn't finish in time; the game was closed. pgr3.log has the details.");
                }
                progress(Phase(watch.Elapsed.TotalSeconds, seconds));
                await Task.Delay(1000);
            }
            progress("Reading the results…");
            var result = Parse(File.ReadAllText(Paths.Log), GameSettings.Load());
            Save(result);
            return result;
        }
        finally
        {
            try { if (savedIni is null) File.Delete(routeIni); else File.WriteAllText(routeIni, savedIni); } catch (IOException) { }
        }
    }

    static string Phase(double t, int seconds)
    {
        if (t < 20) return "Loading the race (the AI drives your car)…";
        if (t < 30) return "Race started: settling in…";
        var left = Math.Max(0, (int)(30 + seconds - t));
        return left > 0 ? $"Measuring: about {left} s left" : "Finishing…";
    }

    static void CopyDir(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (var f in Directory.GetFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(from)) CopyDir(d, Path.Combine(to, Path.GetFileName(d)));
    }

    [GeneratedRegex(@"\[\s*([\d.]+)\] perf lite: ([\d.]+) fps, ([\d.]+) ms/frame \(p99 ([\d.]+) ms, max ([\d.]+) ms\) \| (\d+) draws/frame \| GPU ([\d.]+) ms/frame \| draw thread (-?\d+)% busy")]
    private static partial Regex LiteLine();
    [GeneratedRegex(@"\[\s*([\d.]+)\] long frame \d+: ([\d.]+) ms")]
    private static partial Regex LongLine();
    [GeneratedRegex(@"\[\s*([\d.]+)\] benchmark: measuring (\d+) s")]
    private static partial Regex MeasureLine();
    [GeneratedRegex(@"gpu: ReGPU at (\d+x\d+), msaa (\d+)")]
    private static partial Regex SizeLine();

    static double D(string s) => double.Parse(s, CultureInfo.InvariantCulture);

    /// <summary>The window's perf lite lines (as tools/bench_summary.py).</summary>
    public static Result Parse(string log, GameSettings settings)
    {
        var mark = MeasureLine().Match(log);
        if (!mark.Success)
            throw new InvalidOperationException("The race never reached the measuring part (no \"benchmark: measuring\" line in pgr3.log): the route may have changed, or the game stopped.");
        double t0 = D(mark.Groups[1].Value), t1 = t0 + D(mark.Groups[2].Value);
        var rows = LiteLine().Matches(log).Select(m => m.Groups.Values.Skip(1).Select(g => D(g.Value)).ToArray()).Where(r => r[0] >= t0 && r[0] < t1).ToList();
        if (rows.Count == 0) throw new InvalidOperationException("No performance lines in the measured part of pgr3.log.");
        var longs = LongLine().Matches(log).Count(m => D(m.Groups[1].Value) >= t0 && D(m.Groups[1].Value) < t1);
        var size = SizeLine().Match(log);
        var p99 = rows.Select(r => r[3]).OrderBy(x => x).ToList();
        var options = new List<string>();
        if (settings.Renderer == "d3d12") options.Add("D3D12");
        if (settings.Upscaler != "off") options.Add($"{settings.Upscaler} q{settings.Upscale}");
        if (settings.PerfPreset != "off") options.Add($"preset {settings.PerfPreset}");
        if (settings.Aa != "off") options.Add(settings.Aa);
        if (settings.VramUploadRing) options.Add("ring in VRAM");
        if (settings.DxcShaders) options.Add("DXC");
        if (settings.DrawWorkers) options.Add("draw workers");
        return new Result(
            DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture),
            size.Success ? size.Groups[1].Value : settings.Resolution, size.Success ? int.Parse(size.Groups[2].Value, CultureInfo.InvariantCulture) : settings.Msaa,
            settings.MaxFps, string.Join(", ", options), (int)D(mark.Groups[2].Value),
            Math.Round(rows.Average(r => r[1]), 1), Math.Round(rows.Min(r => r[1]), 1), Math.Round(p99[p99.Count / 2], 2),
            Math.Round(rows.Max(r => r[4]), 2), longs, (int)Math.Round(rows.Average(r => r[5])),
            Math.Round(rows.Average(r => r[6]), 2), Math.Round(rows.Average(r => r[7]), 1));
    }

    static void Save(Result r)
    {
        Directory.CreateDirectory(Dir);
        var name = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
        File.WriteAllText(Path.Combine(Dir, name + ".json"), JsonSerializer.Serialize(r, Json));
        var csv = Path.Combine(Dir, "runs.csv");
        if (!File.Exists(csv))
            File.WriteAllText(csv, "time,resolution,msaa,max_fps,options,seconds,fps_avg,fps_lowest_second,p99_ms,worst_ms,long_frames,draws,gpu_ms,draw_busy\n");
        File.AppendAllText(csv, string.Join(",", r.Time, r.Resolution, r.Msaa, r.MaxFps, $"\"{r.Options}\"", r.Seconds,
            F(r.FpsAvg), F(r.FpsLowestSecond), F(r.P99Ms), F(r.WorstMs), r.LongFrames, r.Draws, F(r.GpuMs), F(r.DrawThreadBusy)) + "\n");
        try { File.Copy(Paths.Log, Path.Combine(Dir, name + ".log"), true); } catch (IOException) { }
    }

    static string F(double v) => v.ToString(CultureInfo.InvariantCulture);

    /// <summary>Earlier results, newest first.</summary>
    public static List<Result> List(int max = 20)
    {
        if (!Directory.Exists(Dir)) return [];
        var list = new List<Result>();
        foreach (var f in new DirectoryInfo(Dir).EnumerateFiles("*.json").OrderByDescending(f => f.Name).Take(max))
            try { if (JsonSerializer.Deserialize<Result>(File.ReadAllText(f.FullName)) is { } r) list.Add(r); } catch (JsonException) { }
        return list;
    }
}
