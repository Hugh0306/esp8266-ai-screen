using System.Net;
using System.Net.Http.Headers;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json;

namespace AIClockBridge;

// Talks to the ESP8266 clock's own HTTP API, so everything the device's web
// page can do (switch display, set bridge host, upload/reset pet GIFs) is
// available straight from the tray. Device address persists in Settings.

class DeviceInfo
{
    public string Ip = "";
    public string Ssid = "";
    public string Bridge = "";
    public string Mode = "auto";       // configured: auto | claude | codex | net | music
    public string Effective = "auto";  // what's actually on screen (AUTO may promote to music)
    public string Showing = "";
    public int SpriteRev;              // bumped by the device on animation change
    public bool ClaudeCustomSprite;
    public bool CodexCustomSprite;
    public int ClaudeW = 111, ClaudeH = 120;
    public int CodexW = 120, CodexH = 120;
}

class DeviceException : Exception
{
    public DeviceException(string message) : base(message) { }
}

static class DeviceClient
{
    const string HostKey = "device_host";
    const string LastSeenKey = "device_last_seen";

    // per-request CancellationTokenSources carry the timeouts (5s info, 8s
    // posts, 30s sprite pull, 60s GIF upload+on-device decode), so the client
    // itself must not impose a shorter global one
    static readonly HttpClient Http = new() { Timeout = Timeout.InfiniteTimeSpan };

    public static string Host
    {
        get => Settings.Get(HostKey);
        set => Settings.Set(HostKey, value);
    }

    /// Last LAN address that polled our /status — i.e. the clock itself.
    public static string LastSeenIp
    {
        get => Settings.Get(LastSeenKey);
        set => Settings.Set(LastSeenKey, value);
    }

    public static Uri BaseUrl
    {
        get
        {
            var h = Host.Trim();
            if (h.Length == 0) return null;
            return Uri.TryCreate(h.StartsWith("http") ? h : $"http://{h}", UriKind.Absolute,
                out var uri) ? uri : null;
        }
    }

    static Uri Resolve(string path)
    {
        var b = BaseUrl ?? throw new DeviceException("未设置设备地址，请先在菜单里填写设备 IP");
        return new Uri(b, path);
    }

    /// GET /api/info
    public static async Task<DeviceInfo> FetchInfo()
    {
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        string body;
        try
        {
            body = await Http.GetStringAsync(Resolve("api/info"), cts.Token);
        }
        catch (DeviceException) { throw; }
        catch (Exception e)
        {
            throw new DeviceException($"无法连接设备：{e.Message}");
        }
        try
        {
            using var doc = JsonDocument.Parse(body);
            var root = doc.RootElement;
            var info = new DeviceInfo
            {
                Ip = Str(root, "ip"),
                Ssid = Str(root, "ssid"),
                Bridge = Str(root, "bridge"),
                Mode = Str(root, "mode", "auto"),
                SpriteRev = Int(root, "sprite_rev"),
                Showing = Str(root, "showing"),
            };
            info.Effective = Str(root, "effective", info.Mode);
            if (root.TryGetProperty("claude", out var claude))
            {
                info.ClaudeCustomSprite = Bool(claude, "custom_sprite");
                info.ClaudeW = Int(claude, "w", 111);
                info.ClaudeH = Int(claude, "h", 120);
            }
            if (root.TryGetProperty("codex", out var codex))
            {
                info.CodexCustomSprite = Bool(codex, "custom_sprite");
                info.CodexW = Int(codex, "w", 120);
                info.CodexH = Int(codex, "h", 120);
            }
            return info;
        }
        catch (Exception)
        {
            throw new DeviceException("设备响应解析失败");
        }
    }

    /// POST /api/display  mode=auto|claude|codex|net|music
    public static Task SetDisplayMode(string mode) =>
        PostForm("api/display", new() { ["mode"] = mode });

    /// POST /api/bridge  host=ip:port
    public static Task SetBridgeHost(string bridgeHost) =>
        PostForm("api/bridge", new() { ["host"] = bridgeHost });

    /// POST /sprite/{claude|codex}  multipart GIF upload — the device decodes
    /// and rescales the GIF on-board, then swaps the animation immediately.
    public static async Task UploadGif(byte[] gif, string slot)
    {
        var url = Resolve($"sprite/{slot}");
        using var content = new MultipartFormDataContent($"aiclock-{Guid.NewGuid()}");
        var filePart = new ByteArrayContent(gif);
        filePart.Headers.ContentType = new MediaTypeHeaderValue("image/gif");
        content.Add(filePart, "file", "pet.gif");
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(60)); // on-device decode
        HttpResponseMessage resp;
        try
        {
            resp = await Http.PostAsync(url, content, cts.Token);
        }
        catch (Exception e)
        {
            throw new DeviceException($"上传失败：{e.Message}");
        }
        using (resp) await ThrowUnlessOk(resp);
    }

    /// POST /sprite/{claude|codex}/reset — back to the compiled-in animation.
    public static Task ResetSprite(string slot) => PostForm($"sprite/{slot}/reset", new());

    /// GET /sprite/{claude|codex}/raw — the animation the device is actually
    /// using, wire format [1 byte frame count][RGB565 big-endian frames...].
    public static async Task<byte[]> FetchSpriteRaw(string slot)
    {
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        byte[] data;
        try
        {
            data = await Http.GetByteArrayAsync(Resolve($"sprite/{slot}/raw"), cts.Token);
        }
        catch (DeviceException) { throw; }
        catch (Exception e)
        {
            throw new DeviceException($"拉取动画失败：{e.Message}");
        }
        if (data.Length <= 1) throw new DeviceException("设备响应解析失败");
        return data;
    }

    // MARK: - internals

    static async Task PostForm(string path, Dictionary<string, string> fields)
    {
        var url = Resolve(path);
        using var content = new FormUrlEncodedContent(fields);
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(8));
        HttpResponseMessage resp;
        try
        {
            resp = await Http.PostAsync(url, content, cts.Token);
        }
        catch (Exception e)
        {
            throw new DeviceException($"请求失败：{e.Message}");
        }
        using (resp) await ThrowUnlessOk(resp);
    }

    static async Task ThrowUnlessOk(HttpResponseMessage resp)
    {
        if (resp.IsSuccessStatusCode) return;
        var msg = "";
        try { msg = await resp.Content.ReadAsStringAsync(); } catch { }
        throw new DeviceException($"设备返回 HTTP {(int)resp.StatusCode} {msg}");
    }

    static string Str(JsonElement o, string k, string dflt = "")
        => o.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.String
            ? v.GetString() : dflt;

    static int Int(JsonElement o, string k, int dflt = 0)
        => o.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.Number
            ? (int)v.GetDouble() : dflt;

    static bool Bool(JsonElement o, string k)
        => o.TryGetProperty(k, out var v)
            && (v.ValueKind == JsonValueKind.True || v.ValueKind == JsonValueKind.False)
            && v.GetBoolean();

    // MARK: - discovery / pairing

    /// Checks whether `ip` answers like our clock (GET /api/info with the
    /// expected JSON shape).
    public static async Task<bool> VerifyDevice(string ip, double timeoutSeconds = 2)
    {
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(timeoutSeconds));
            var body = await Http.GetStringAsync($"http://{ip}/api/info", cts.Token);
            using var doc = JsonDocument.Parse(body);
            return doc.RootElement.TryGetProperty("mode", out var mode)
                && mode.ValueKind == JsonValueKind.String
                && doc.RootElement.TryGetProperty("sprite_rev", out _);
        }
        catch
        {
            return false;
        }
    }

    /// Finds the clock and pairs (sets Host). Strategy:
    ///  1. the address that most recently polled our /status (no scanning);
    ///  2. the currently configured host, re-verified;
    ///  3. sweep this PC's /24 subnet for /api/info (covers a factory-fresh
    ///     device that has WiFi but no bridge configured yet).
    public static async Task<string> AutoPair(Action<string> progress)
    {
        var candidates = new List<string>();
        if (LastSeenIp.Length > 0) candidates.Add(LastSeenIp);
        var configured = Host.Split(':')[0];
        if (configured.Length > 0 && !candidates.Contains(configured)) candidates.Add(configured);

        foreach (var ip in candidates)
        {
            progress($"验证 {ip}…");
            if (await VerifyDevice(ip))
            {
                Host = ip;
                return ip;
            }
        }
        return await ScanSubnet(progress);
    }

    /// Parallel sweep of the local /24 (254 hosts, ~0.8s timeout each,
    /// 32-wide). Only used when the passive route came up empty.
    static async Task<string> ScanSubnet(Action<string> progress)
    {
        var myIp = LocalIPv4();
        var dot = myIp?.LastIndexOf('.') ?? -1;
        if (myIp == null || dot < 0) return null;
        var prefix = myIp[..dot];
        progress($"扫描 {prefix}.1-254…");

        using var sem = new SemaphoreSlim(32);
        string found = null;
        var tasks = new List<Task>();
        for (int n = 1; n <= 254; n++)
        {
            var ip = $"{prefix}.{n}";
            if (ip == myIp) continue;
            tasks.Add(Task.Run(async () =>
            {
                await sem.WaitAsync();
                try
                {
                    if (Volatile.Read(ref found) != null) return;
                    if (await VerifyDevice(ip, 0.8))
                        Interlocked.CompareExchange(ref found, ip, null);
                }
                finally
                {
                    sem.Release();
                }
            }));
        }
        await Task.WhenAll(tasks);
        if (found != null) Host = found;
        return found;
    }

    /// LAN IPv4 of this PC — used for one-click "point the device's bridge at
    /// this machine". Prefers an interface that has a default gateway.
    public static string LocalIPv4()
    {
        string best = null;
        try
        {
            foreach (var nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.OperationalStatus != OperationalStatus.Up) continue;
                if (nic.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
                var props = nic.GetIPProperties();
                var hasGateway = props.GatewayAddresses
                    .Any(g => g.Address?.AddressFamily == AddressFamily.InterNetwork);
                foreach (var addr in props.UnicastAddresses)
                {
                    if (addr.Address.AddressFamily != AddressFamily.InterNetwork) continue;
                    if (IPAddress.IsLoopback(addr.Address)) continue;
                    var ip = addr.Address.ToString();
                    if (hasGateway) return ip;
                    best ??= ip;
                }
            }
        }
        catch
        {
            // fall through
        }
        return best;
    }
}
