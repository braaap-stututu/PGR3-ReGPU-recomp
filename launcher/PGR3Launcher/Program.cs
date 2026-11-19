[assembly: System.Runtime.Versioning.SupportedOSPlatform("windows")]

namespace PGR3Launcher;

internal static class Program
{
    const string Title = "Project Gotham Racing 3 ReGPU";

    [STAThread]
    static int Main(string[] args)
    {
        // --play: start the game with the saved settings, without the window (shortcuts).
        if (args.Contains("--play"))
        {
            try
            {
                Game.Launch()?.Dispose();
                return 0;
            }
            catch (Exception ex) { Platform.Error(Title, ex.Message); return 1; }
        }
        // --benchmark [seconds]: the benchmark race without the window; the result to the console.
        var bench = Array.IndexOf(args, "--benchmark");
        if (bench >= 0)
        {
            if (GetStdHandle(-11) == IntPtr.Zero) AttachConsole(-1);
            try
            {
                var seconds = bench + 1 < args.Length && int.TryParse(args[bench + 1], out var s) ? s : 60;
                var last = "";
                var r = Benchmark.Run(seconds, t => { if (t != last) Console.WriteLine(last = t); }).GetAwaiter().GetResult();
                Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(r, new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
                return 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine("Error: " + ex.Message);
                return 1;
            }
        }
        // One window: starting the launcher again brings the open one forward.
        using var single = new Mutex(true, "PGR3ReGPU.Launcher", out bool first);
        if (!first)
        {
            foreach (var p in System.Diagnostics.Process.GetProcessesByName("PGR3Launcher"))
                if (p.Id != Environment.ProcessId && p.MainWindowHandle != IntPtr.Zero) SetForegroundWindow(p.MainWindowHandle);
            return 0;
        }
        Photino.NET.PhotinoWindow window;
        var (w, h) = DefaultSize(1280, 860);
        try { window = Web.Create(Title, "index.html", w, h, 960, 640); }
        catch (Exception ex)
        {
            Platform.Error(Title, "The launcher needs Microsoft Edge WebView2 (part of Windows 11).\n\n" + ex.Message);
            return 1;
        }
        _ = new Bridge(new Shell(window));
        window.WaitForClose();
        return 0;
    }

    /// <summary>The window's first size at 100% display scaling, scaled with Windows' scaling, at most 92% of the screen.</summary>
    static (int, int) DefaultSize(int width, int height)
    {
        try
        {
            var scale = Math.Max(1.0, GetDpiForSystem() / 96.0);
            int maxW = (int)(GetSystemMetrics(16) * 0.92), maxH = (int)(GetSystemMetrics(17) * 0.92);
            return (Math.Min((int)(width * scale), maxW), Math.Min((int)(height * scale), maxH));
        }
        catch { return (width, height); }
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern uint GetDpiForSystem();

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern int GetSystemMetrics(int index);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool SetForegroundWindow(IntPtr hWnd);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern bool AttachConsole(int processId);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern IntPtr GetStdHandle(int which);
}
