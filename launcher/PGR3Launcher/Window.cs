using Photino.NET;

namespace PGR3Launcher;

/// <summary>
/// The launcher window: Photino showing wwwroot on WebView2. Pages come from the launcher's own scheme handler at
/// https://launcher.pgr3/ (no server, nothing on the network). From the FM3 launcher's Window.cs, Windows only.
/// </summary>
public static class Web
{
    public const string Base = "https://launcher.pgr3/";

    public static PhotinoWindow Create(string title, string page, int width, int height, int minWidth, int minHeight)
    {
        var w = new PhotinoWindow()
            .SetTitle(title)
            .SetSize(width, height)
            .SetMinSize(minWidth, minHeight)
            .SetUseOsDefaultSize(false)
            .Center()
            .SetLogVerbosity(0)
            .SetContextMenuEnabled(false)
            .SetDevToolsEnabled(System.Diagnostics.Debugger.IsAttached || Environment.GetEnvironmentVariable("PGR3_LAUNCHER_DEVTOOLS") == "1")
            .SetJavascriptClipboardAccessEnabled(true)
            .SetTemporaryFilesPath(Path.Combine(Path.GetTempPath(), "PGR3Launcher-webview"))
            .RegisterCustomSchemeHandler("https", Serve);
        w.Load(new Uri(Base + page));
        return w;
    }

    // Only the launcher's host: anything else gets nothing.
    static Stream? Serve(object sender, string scheme, string url, out string contentType)
    {
        contentType = "text/plain";
        if (!url.StartsWith(Base, StringComparison.OrdinalIgnoreCase)) return null;
        var rel = Uri.UnescapeDataString(new Uri(url).AbsolutePath.TrimStart('/'));
        var root = Path.Combine(AppContext.BaseDirectory, "wwwroot");
        contentType = Path.GetExtension(rel).ToLowerInvariant() switch
        {
            ".html" => "text/html",
            ".js" => "text/javascript",
            ".css" => "text/css",
            ".png" => "image/png",
            ".svg" => "image/svg+xml",
            _ => "application/octet-stream",
        };
        var full = Path.GetFullPath(Path.Combine(root, rel));
        if (!full.StartsWith(Path.GetFullPath(root) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) || !File.Exists(full)) return null;
        try { return contentType == "text/html" ? Stamped(full, root) : File.OpenRead(full); } catch { return null; }
    }

    // Scripts and stylesheets named by their file's time ("app.js?v=<ticks>"): WebView2 caches them (the handler can't
    // send headers), so an updated launcher would go on running the old app.js.
    static Stream Stamped(string page, string root)
    {
        var html = System.Text.RegularExpressions.Regex.Replace(File.ReadAllText(page), @"(src|href)=""([\w.-]+\.(?:js|css))""", m =>
        {
            var file = Path.Combine(root, m.Groups[2].Value);
            return File.Exists(file) ? $"{m.Groups[1].Value}=\"{m.Groups[2].Value}?v={File.GetLastWriteTimeUtc(file).Ticks}\"" : m.Value;
        });
        return new MemoryStream(System.Text.Encoding.UTF8.GetBytes(html));
    }
}

/// <summary>What the page runs in: the launcher's window. (FM3's launcher also has a browser shell for Wine.)</summary>
public sealed class Shell(PhotinoWindow window)
{
    public void Send(string json) => window.SendWebMessage(json);
    public void OnMessage(Action<string> handler) => window.RegisterWebMessageReceivedHandler((_, text) => handler(text));
    public Task<string[]?> PickFolder(string title, string? start) => window.ShowOpenFolderAsync(title, start, false)!;
}

/// <summary>Opening folders and message boxes.</summary>
public static class Platform
{
    public static void OpenFolder(string path) => System.Diagnostics.Process.Start("explorer.exe", $"\"{path}\"");

    public static void Error(string title, string text) => MessageBoxW(IntPtr.Zero, text, title, 0x10);

    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    static extern int MessageBoxW(IntPtr hwnd, string text, string caption, uint type);
}
