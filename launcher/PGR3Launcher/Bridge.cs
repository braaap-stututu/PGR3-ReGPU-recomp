using System.Text.Json;
using System.Text.Json.Nodes;

namespace PGR3Launcher;

/// <summary>
/// The page's calls: { id, cmd, args } over the window's message channel (wwwroot/host.js), answered with
/// { id, ok, result | error }; progress as { event, data }. From the FM3 launcher's Bridge.cs.
/// </summary>
public sealed class Bridge
{
    static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    readonly Shell _window;

    public Bridge(Shell shell)
    {
        _window = shell;
        shell.OnMessage(OnMessage);
    }

    async void OnMessage(string text)
    {
        JsonNode? msg;
        try { msg = JsonNode.Parse(text); } catch { return; }
        var id = msg?["id"]?.GetValue<int>() ?? 0;
        var cmd = msg?["cmd"]?.GetValue<string>() ?? "";
        var args = msg?["args"];
        try
        {
            var result = await Run(cmd, args);
            Post(new { id, ok = true, result });
        }
        catch (Exception ex)
        {
            Post(new { id, ok = false, error = ex.Message });
        }
    }

    void Post(object o) => _window.Send(JsonSerializer.Serialize(o, Json));

    async Task<object?> Run(string cmd, JsonNode? a)
    {
        switch (cmd)
        {
            case "state": return State();
            case "save":
                SaveFromPage(a.Deserialize<GameSettings>(Json)!);
                return State();
            // The game folder changes only by a pick or Reset, never by the page's copy of the settings.
            case "game.reset":
                {
                    var s = GameSettings.Load();
                    s.GameFolder = "";
                    s.Save();
                    return State();
                }
            case "game.browse":
                {
                    var picked = await _window.PickFolder("Choose the game folder (the extracted disc, with default.xex)", Directory.Exists(Paths.GameDir) ? Paths.GameDir : null);
                    if (picked is { Length: > 0 })
                    {
                        await Task.Run(() => Xex.Require(picked[0]));
                        var s = GameSettings.Load();
                        s.GameFolder = picked[0];
                        s.Save();
                    }
                    return State();
                }
            case "xex.check":
                await Task.Run(() => Xex.CheckNow(Paths.GameDir));
                return State();
            case "launch":
                if (a?["settings"].Deserialize<GameSettings>(Json) is { } toSave) SaveFromPage(toSave);
                Game.Launch()?.Dispose();
                return null;
            case "benchmark.run":
                {
                    var seconds = a?["seconds"]?.GetValue<int>() ?? 60;
                    var result = await Benchmark.Run(seconds, text => Post(new { @event = "benchmark", data = new { text } }));
                    return new { result, history = Benchmark.List() };
                }
            case "benchmark.list": return Benchmark.List();
            case "shaders.delete":
                ShaderCache.Delete();
                return State();
            case "open":
                {
                    var path = a?.GetValue<string>() switch
                    {
                        "user" => Directory.CreateDirectory(Paths.User).FullName,
                        "install" => Paths.Root,
                        "game" => Paths.GameDir,
                        "log" => Directory.CreateDirectory(Paths.RunDir).FullName,
                        "bench" => Directory.CreateDirectory(Benchmark.Dir).FullName,
                        _ => throw new ArgumentException("Unknown folder."),
                    };
                    Platform.OpenFolder(path);
                    return null;
                }
            default:
                throw new ArgumentException($"Unknown command {cmd}.");
        }
    }

    static void SaveFromPage(GameSettings fromPage)
    {
        fromPage.GameFolder = GameSettings.Load().GameFolder;
        fromPage.Save();
    }

    static object State()
    {
        var settings = GameSettings.Load();
        return new
        {
            settings,
            version = System.Reflection.CustomAttributeExtensions.GetCustomAttribute<System.Reflection.AssemblyInformationalVersionAttribute>(typeof(Bridge).Assembly)?.InformationalVersion.Split('+')[0] ?? "0.0.0",
            paths = new { root = Paths.Root, user = Paths.User, exe = Paths.Exe, gameDefault = Paths.GameDirDefault, gameDir = Paths.GameDir, gameFromEnv = Paths.GameDirFromEnv, log = Paths.Log },
            checks = new
            {
                exe = File.Exists(Paths.Exe),
                vulkan = File.Exists(Paths.ExeFor("vulkan")),
                d3d12 = File.Exists(Paths.ExeFor("d3d12")),
                exeFromEnv = Environment.GetEnvironmentVariable("PGR3_EXE") is { Length: > 0 },
                game = File.Exists(Path.Combine(Paths.GameDir, "default.xex")),
                xex = Xex.Status(Paths.GameDir),
                running = Game.Running,
                benchmark = Benchmark.Available,
            },
            options = new
            {
                resolutions = Options.Resolutions(),
                gpus = Options.Gpus().Select(g => new { value = g.Name, name = g.MemoryMb >= 1024 ? $"{g.Name} ({g.MemoryMb / 1024.0:0.#} GB)" : $"{g.Name} (uses system memory)" }),
                aspects = Options.Aspects.Select(x => new { value = x.Value, name = x.Name }),
                fps = Options.Fps,
                msaa = Options.Msaa.Select(x => new { value = x.Value, name = x.Name }),
                aa = Options.Aa.Select(x => new { value = x.Value, name = x.Name }),
                ssaa = Options.Ssaa.Select(x => new { value = x.Value, name = x.Name }),
                upscalers = Options.Upscalers.Select(x => new { value = x.Value, name = x.Name }),
                upscale = Options.Upscale.Select(x => new { value = x.Value, name = x.Name }),
                sharpening = Options.Sharpening.Select(x => new { value = x.Value, name = x.Name }),
                languages = Options.Languages.Select(x => new { value = x.Value, name = x.Name }),
            },
            shaders = ShaderCache.Get(),
        };
    }
}
