// JoypadLive — C# REST bridge for live controller-remap crowd control.
//
// Parity port of tools/joypad-live/python/server.py — same wire protocol, same
// HTTP contract — so Streamer.bot / curl / anything hitting localhost:8777
// gets identical responses whether you run the Python bridge or this one.
//
//   dotnet run -- /dev/cu.usbmodemXXXX                    # macOS / Linux
//   dotnet run -- COM5                                    # Windows
//   dotnet run -- /dev/cu.usbmodemXXXX --define-demo      # install AswapB + Chaos
//
// Endpoints:
//   GET  /health          { ok, port }
//   GET  /info            INFO
//   GET  /profiles        PROFILE.LIST
//   POST /profile/<n>     PROFILE.SET { index:n }
//   POST /neutral         PROFILE.SET { index:0 }
//   POST /save            PROFILE.SAVE — JSON body forwarded as args
//   POST /apply           PROFILE.APPLY — ephemeral RAM-only remap (no flash write)
//   POST /clear           PROFILE.CLEAR — drop the ephemeral override
//
// Only one process may own the CDC port at a time. Close config.joypad.ai
// (browser Web Serial) before running this.

using System.Collections.Concurrent;
using System.IO.Ports;
using System.Net;
using System.Text;
using System.Text.Json;

namespace JoypadLive;

public static class Program
{
    // From src/usb/usbd/cdc/cdc_protocol.h
    const byte SYNC    = 0xAA;
    const byte MSG_CMD = 0x01;
    const byte MSG_RSP = 0x02;

    static SerialPort _sp = null!;
    static readonly object _lock = new();

    // ========================================================================
    // Event broadcaster — drives the viewer overlay's live activity feed via
    // SSE on GET /events. Each command endpoint publishes a {kind, label, user,
    // platform, ts} event after success. Bots forward X-User / X-Platform
    // headers so the feed shows e.g. "alice@twitch → tap A" in real time.
    // Mirrors the Python bridge's EventBroadcaster 1:1.
    // ========================================================================

    const int EventHistoryMax = 50;
    static readonly object _evLock = new();
    static readonly LinkedList<Dictionary<string, object?>> _evHistory = new();
    static readonly List<BlockingCollection<Dictionary<string, object?>>> _evSubs = new();

    static void PublishEvent(string kind, string label, string? user, string? platform)
    {
        var ev = new Dictionary<string, object?>
        {
            ["ts"]       = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            ["kind"]     = kind,
            ["label"]    = label,
            ["user"]     = user ?? "anonymous",
            ["platform"] = platform ?? "?",
        };
        lock (_evLock)
        {
            _evHistory.AddLast(ev);
            while (_evHistory.Count > EventHistoryMax) _evHistory.RemoveFirst();
            foreach (var sub in _evSubs)
            {
                try { sub.TryAdd(ev); } catch { /* sub disposed mid-publish */ }
            }
        }
    }

    static (BlockingCollection<Dictionary<string, object?>>, List<Dictionary<string, object?>>) SubscribeEvents()
    {
        var q = new BlockingCollection<Dictionary<string, object?>>(boundedCapacity: 100);
        lock (_evLock)
        {
            _evSubs.Add(q);
            return (q, new List<Dictionary<string, object?>>(_evHistory));
        }
    }

    static void UnsubscribeEvents(BlockingCollection<Dictionary<string, object?>> q)
    {
        lock (_evLock) { _evSubs.Remove(q); }
        try { q.Dispose(); } catch { /* already disposed */ }
    }

    // Read viewer attribution from request headers (set by the chat bots).
    static (string user, string platform) Attrib(HttpListenerRequest req)
    {
        return (
            req.Headers["X-User"]     ?? "anonymous",
            req.Headers["X-Platform"] ?? "?"
        );
    }

    // Pull an int out of a deserialized JSON body (Dictionary<string, object?>
    // where values are JsonElements). Used to summarize overlay flag bitmaps.
    static long JsonInt(Dictionary<string, object?> d, string key, long fallback = 0)
    {
        if (!d.TryGetValue(key, out var v) || v == null) return fallback;
        if (v is JsonElement je && je.ValueKind == JsonValueKind.Number) return je.GetInt64();
        if (v is long l) return l;
        if (v is int i) return i;
        return fallback;
    }

    // Friendly button name → JP_BUTTON_* bitmask. Used by /press, /hold.
    static readonly Dictionary<string, uint> BUTTON_NAMES = new(StringComparer.OrdinalIgnoreCase)
    {
        ["a"]=1, ["cross"]=1, ["b1"]=1,
        ["b"]=2, ["circle"]=2, ["b2"]=2,
        ["x"]=4, ["square"]=4, ["b3"]=4,
        ["y"]=8, ["triangle"]=8, ["b4"]=8,
        ["l1"]=16, ["lb"]=16,
        ["r1"]=32, ["rb"]=32,
        ["l2"]=64, ["lt"]=64,
        ["r2"]=128, ["rt"]=128,
        ["select"]=256, ["back"]=256, ["minus"]=256, ["share"]=256, ["s1"]=256,
        ["start"]=512, ["plus"]=512, ["options"]=512, ["s2"]=512,
        ["l3"]=1024, ["ls"]=1024,
        ["r3"]=2048, ["rs"]=2048,
        ["up"]=4096, ["u"]=4096, ["du"]=4096,
        ["down"]=8192, ["d"]=8192, ["dd"]=8192,
        ["left"]=16384, ["l"]=16384, ["dl"]=16384,
        ["right"]=32768, ["r"]=32768, ["dr"]=32768,
        ["home"]=65536, ["guide"]=65536, ["ps"]=65536, ["a1"]=65536,
    };

    const int TAP_MS = 80;  // default /press tap duration

    public static int Main(string[] args)
    {
        string? port = null;
        int baud = 115200;
        int httpPort = 8777;
        bool defineDemo = false;
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--baud":        baud = int.Parse(args[++i]); break;
                case "--http-port":   httpPort = int.Parse(args[++i]); break;
                case "--define-demo": defineDemo = true; break;
                default:              port ??= args[i]; break;
            }
        }
        if (port == null)
        {
            Console.Error.WriteLine("Usage: dotnet run -- <port> [--baud N] [--http-port N] [--define-demo]");
            Console.Error.WriteLine("  <port>: e.g. /dev/cu.usbmodemXXXX or COM5");
            return 1;
        }

        try
        {
            // DTR/RTS asserted on open. pyserial does this by default on POSIX;
            // System.IO.Ports does NOT, and TinyUSB CDC on RP2040 needs DTR=true
            // before it will respond to commands.
            _sp = new SerialPort(port, baud)
            {
                ReadTimeout = 100,
                DtrEnable = true,
                RtsEnable = true,
            };
            _sp.Open();
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"Failed to open {port}: {e.Message}");
            return 1;
        }

        var info = Cmd("INFO");
        Console.WriteLine($"Connected {port}: {ToJson(info)}");
        if (info.TryGetValue("error", out var err))
            Console.Error.WriteLine($"WARNING: {err} — is this the adapter's CDC port, "
                + "and is something else (config.joypad.ai) holding it?");

        return defineDemo ? DefineDemo() : ServeHttp(port, httpPort);
    }

    // -------- demo profiles --------
    // button_map = 18 entries: position = source button, value = target (0=passthrough,
    // 1..24=remap to button N 1-based, 255=disable). See .dev/docs/streamer-live-remap.md §4.
    static readonly (string Name, int[] Map)[] Demos =
    {
        // A<->B swap: pos0(B1)->2(B2), pos1(B2)->1(B1)
        ("AswapB", new[] { 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0, 0, 0 }),
        // D-pad scramble: U->L(15), D->R(16), L->D(14), R->U(13)
        ("Chaos",  new[] { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 16, 14, 13, 0, 0 }),
    };

    static int DefineDemo()
    {
        Console.WriteLine("Defining demo profiles...");
        foreach (var (name, map) in Demos)
        {
            var r = Cmd("PROFILE.SAVE", new() { ["index"] = 255, ["name"] = name, ["button_map"] = map });
            Console.WriteLine($"  PROFILE.SAVE {name,-8} -> {ToJson(r)}");
        }
        Console.WriteLine("Current profiles:");
        Console.WriteLine("  " + ToJson(Cmd("PROFILE.LIST")));
        return 0;
    }

    // -------- HTTP server --------
    static int ServeHttp(string port, int httpPort)
    {
        var http = new HttpListener();
        http.Prefixes.Add($"http://127.0.0.1:{httpPort}/");
        http.Start();
        Console.WriteLine($"REST API on http://127.0.0.1:{httpPort}/  "
            + "(GET /profiles | POST /profile/<n> | POST /neutral)");
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; http.Stop(); };

        // Each request runs on a worker thread so long-lived SSE connections
        // (GET /events) don't block other handlers.
        while (http.IsListening)
        {
            HttpListenerContext ctx;
            try { ctx = http.GetContext(); } catch { break; }
            ThreadPool.QueueUserWorkItem(_ => {
                try { HandleRequest(ctx, port); }
                catch (Exception e) { Console.Error.WriteLine($"handler error: {e.Message}"); }
            });
        }
        return 0;
    }

    // Lifted out so PublishEvent hooks can early-bail when Cmd() returned an
    // error from the firmware (matches Python's `r.get("ok")` check).
    static bool IsOk(Dictionary<string, object?> resp)
    {
        if (resp.TryGetValue("ok", out var v))
        {
            if (v is bool b) return b;
            if (v is JsonElement je && je.ValueKind == JsonValueKind.True) return true;
        }
        // Some commands (INFO, etc.) don't return an "ok" key. Treat the
        // absence of an "error" key as success too.
        return !resp.ContainsKey("error");
    }

    static void WriteSse(HttpListenerContext ctx, Dictionary<string, object?> ev)
    {
        WriteRaw(ctx, "data: " + JsonSerializer.Serialize(ev) + "\n\n");
    }

    static void WriteRaw(HttpListenerContext ctx, string s)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(s);
        ctx.Response.OutputStream.Write(bytes, 0, bytes.Length);
        ctx.Response.OutputStream.Flush();
    }

    static void HandleRequest(HttpListenerContext ctx, string port)
    {
        string path = ctx.Request.Url!.AbsolutePath.Trim('/');
        string method = ctx.Request.HttpMethod;
        Dictionary<string, object?> resp;
        int code = 200;
        // Static HTML pages copied next to the binary by JoypadLive.csproj.
        //   /  or  /dashboard → dashboard.html  (streamer's manual control)
        //   /overlay          → overlay.html    (viewer OBS browser source)
        if (method == "GET" && (path == "" || path == "dashboard" || path == "overlay"))
        {
            string file = (path == "overlay") ? "overlay.html" : "dashboard.html";
            string htmlPath = Path.Combine(AppContext.BaseDirectory, file);
            if (File.Exists(htmlPath))
            {
                byte[] html = File.ReadAllBytes(htmlPath);
                ctx.Response.StatusCode = 200;
                ctx.Response.ContentType = "text/html; charset=utf-8";
                ctx.Response.AppendHeader("Access-Control-Allow-Origin", "*");
                ctx.Response.OutputStream.Write(html, 0, html.Length);
                ctx.Response.Close();
                Console.WriteLine($"{method} /{path} -> {file} ({html.Length} bytes)");
                return;
            }
            // fall through to 404 below
        }

        // SSE — chunked stream of {ts, kind, label, user, platform} JSON
        // events. Overlay page (overlay.html) subscribes via EventSource;
        // bots publish via every command endpoint.
        if (method == "GET" && path == "events")
        {
            ctx.Response.StatusCode = 200;
            ctx.Response.ContentType = "text/event-stream; charset=utf-8";
            ctx.Response.Headers["Cache-Control"] = "no-cache";
            ctx.Response.AppendHeader("Access-Control-Allow-Origin", "*");
            ctx.Response.AppendHeader("X-Accel-Buffering", "no");
            ctx.Response.SendChunked = true;

            var (q, history) = SubscribeEvents();
            try
            {
                // Replay history so a reconnect doesn't show an empty feed.
                foreach (var ev in history) WriteSse(ctx, ev);
                // Stream new events; emit a heartbeat comment every 15s so
                // OBS/proxies keep the connection open.
                while (true)
                {
                    if (q.TryTake(out var ev, TimeSpan.FromSeconds(15)))
                        WriteSse(ctx, ev);
                    else
                        WriteRaw(ctx, ": heartbeat\n\n");
                }
            }
            catch (HttpListenerException) { /* client disconnected */ }
            catch (IOException) { /* socket closed */ }
            catch (ObjectDisposedException) { }
            finally
            {
                UnsubscribeEvents(q);
                try { ctx.Response.Close(); } catch { }
            }
            return;
        }
        try
        {
            if (method == "GET" && path == "health")
                resp = new() { ["ok"] = true, ["port"] = port };
            else if (method == "GET" && path == "info")
                resp = Cmd("INFO");
            else if (method == "GET" && path == "profiles")
                resp = Cmd("PROFILE.LIST");
            // Hot-path selection uses PROFILE.SELECT — RAM only, no flash
            // write. The persistent boot default survives via /default/<n>.
            else if (method == "POST" && path.StartsWith("profile/")
                     && int.TryParse(path[8..], out int n))
            {
                resp = Cmd("PROFILE.SELECT", new() { ["index"] = n });
                if (IsOk(resp))
                {
                    var (u, p) = Attrib(ctx.Request);
                    string name = (resp.TryGetValue("name", out var nv) ? nv?.ToString() : null) ?? $"profile {n}";
                    PublishEvent("effect", name, u, p);
                }
            }
            else if (method == "POST" && path == "neutral")
            {
                resp = Cmd("PROFILE.SELECT", new() { ["index"] = 0 });
                if (IsOk(resp))
                {
                    var (u, p) = Attrib(ctx.Request);
                    PublishEvent("reset", "neutral", u, p);
                }
            }
            else if (method == "POST" && path.StartsWith("default/")
                     && int.TryParse(path[8..], out int dn))
                // Persistent — writes to flash. For "make this the new boot
                // default," not for hot-path crowd-control switching.
                resp = Cmd("PROFILE.SET", new() { ["index"] = dn });
            else if (method == "POST" && path == "save")
            {
                using var reader = new StreamReader(ctx.Request.InputStream);
                var body = JsonSerializer.Deserialize<Dictionary<string, object?>>(reader.ReadToEnd()) ?? new();
                resp = Cmd("PROFILE.SAVE", body);
            }
            else if (method == "POST" && path == "apply")
            {
                using var reader = new StreamReader(ctx.Request.InputStream);
                var body = JsonSerializer.Deserialize<Dictionary<string, object?>>(reader.ReadToEnd()) ?? new();
                resp = Cmd("PROFILE.APPLY", body);
                if (IsOk(resp))
                {
                    var (u, p) = Attrib(ctx.Request);
                    string label =
                        (resp.TryGetValue("name", out var nv) ? nv?.ToString() : null)
                        ?? (body.TryGetValue("name", out var bnv) ? bnv?.ToString() : null)
                        ?? "apply";
                    PublishEvent("effect", label, u, p);
                }
            }
            else if (method == "POST" && path == "clear")
            {
                resp = Cmd("PROFILE.CLEAR");
                if (IsOk(resp))
                {
                    var (u, p) = Attrib(ctx.Request);
                    PublishEvent("reset", "clear apply", u, p);
                }
            }
            else if (method == "POST" && path == "overlay")
            {
                using var reader = new StreamReader(ctx.Request.InputStream);
                var body = JsonSerializer.Deserialize<Dictionary<string, object?>>(reader.ReadToEnd()) ?? new();
                resp = Cmd("OVERLAY.SET", body);
                if (IsOk(resp))
                {
                    var (u, p) = Attrib(ctx.Request);
                    long flags = JsonInt(body, "flags");
                    long socd  = JsonInt(body, "socd_mode");
                    var parts = new List<string>();
                    if ((flags & 1)  != 0) parts.Add("swap sticks");
                    if ((flags & 2)  != 0) parts.Add("invert LY");
                    if ((flags & 4)  != 0) parts.Add("invert RY");
                    if ((flags & 8)  != 0) parts.Add("invert LX");
                    if ((flags & 16) != 0) parts.Add("invert RX");
                    if (socd != 0) parts.Add($"SOCD {socd}");
                    string label = parts.Count > 0 ? string.Join(" + ", parts) : "overlay";
                    PublishEvent("overlay", label, u, p);
                }
            }
            else if (method == "POST" && path == "overlay/clear")
            {
                resp = Cmd("OVERLAY.CLEAR");
                if (IsOk(resp))
                {
                    var (u, p) = Attrib(ctx.Request);
                    PublishEvent("reset", "clear overlay", u, p);
                }
            }
            else if (method == "POST" && path.StartsWith("press/"))
            {
                string name = path[6..];
                if (!BUTTON_NAMES.TryGetValue(name, out uint mask))
                {
                    code = 400;
                    resp = new() { ["ok"] = false, ["error"] = $"unknown button: {name}" };
                }
                else
                {
                    Cmd("INPUT.INJECT", new() { ["buttons"] = mask });
                    Thread.Sleep(TAP_MS);
                    Cmd("INPUT.INJECT", new() { ["buttons"] = 0 });
                    var (u, p) = Attrib(ctx.Request);
                    PublishEvent("press", $"tap {name.ToUpper()}", u, p);
                    resp = new() { ["ok"] = true, ["tapped"] = name, ["mask"] = mask };
                }
            }
            else if (method == "POST" && path.StartsWith("hold/"))
            {
                string name = path[5..];
                if (!BUTTON_NAMES.TryGetValue(name, out uint mask))
                {
                    code = 400;
                    resp = new() { ["ok"] = false, ["error"] = $"unknown button: {name}" };
                }
                else
                {
                    resp = Cmd("INPUT.INJECT", new() { ["buttons"] = mask });
                    var (u, p) = Attrib(ctx.Request);
                    PublishEvent("press", $"hold {name.ToUpper()}", u, p);
                }
            }
            else if (method == "POST" && path == "release")
            {
                resp = Cmd("INPUT.INJECT", new() { ["buttons"] = 0 });
                if (IsOk(resp))
                {
                    var (u, p) = Attrib(ctx.Request);
                    PublishEvent("reset", "release", u, p);
                }
            }
            else if (method == "POST" && path == "inject")
            {
                using var reader = new StreamReader(ctx.Request.InputStream);
                var body = JsonSerializer.Deserialize<Dictionary<string, object?>>(reader.ReadToEnd()) ?? new();
                resp = Cmd("INPUT.INJECT", body);
            }
            else
            {
                code = 404;
                resp = new() { ["ok"] = false, ["error"] = "not found" };
            }
        }
        catch (Exception ex)
        {
            code = 500;
            resp = new() { ["ok"] = false, ["error"] = ex.Message };
        }
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(resp);
        ctx.Response.StatusCode = code;
        ctx.Response.ContentType = "application/json";
        ctx.Response.AppendHeader("Access-Control-Allow-Origin", "*");
        ctx.Response.OutputStream.Write(bytes, 0, bytes.Length);
        ctx.Response.Close();
        Console.WriteLine($"{method} /{path} -> {Encoding.UTF8.GetString(bytes)}");
    }

    // -------- CDC framing --------
    // Frame: [SYNC=0xAA][LEN:2 LE][TYPE][SEQ][PAYLOAD][CRC16:2 LE]
    // CRC-16-CCITT (poly 0x1021, init 0xFFFF) over TYPE+SEQ+PAYLOAD.
    static ushort Crc16(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte b in data)
        {
            crc ^= (ushort)(b << 8);
            for (int i = 0; i < 8; i++)
                crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021)
                                          : (ushort)(crc << 1);
        }
        return crc;
    }

    static Dictionary<string, object?> Cmd(string cmd, Dictionary<string, object?>? args = null)
    {
        var payload = new Dictionary<string, object?> { ["cmd"] = cmd };
        if (args != null)
            foreach (var kv in args) payload[kv.Key] = kv.Value;
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(payload);

        byte type = MSG_CMD, seq = 1;
        byte[] crcIn = new byte[2 + json.Length];
        crcIn[0] = type; crcIn[1] = seq;
        Buffer.BlockCopy(json, 0, crcIn, 2, json.Length);
        ushort crc = Crc16(crcIn);

        byte[] frame = new byte[5 + json.Length + 2];
        int i = 0;
        frame[i++] = SYNC;
        frame[i++] = (byte)(json.Length & 0xFF);
        frame[i++] = (byte)((json.Length >> 8) & 0xFF);
        frame[i++] = type;
        frame[i++] = seq;
        Buffer.BlockCopy(json, 0, frame, i, json.Length); i += json.Length;
        frame[i++] = (byte)(crc & 0xFF);
        frame[i++] = (byte)((crc >> 8) & 0xFF);

        lock (_lock)
        {
            _sp.DiscardInBuffer();
            _sp.Write(frame, 0, i);

            byte[] buf = new byte[1024];
            int got = 0;
            long deadline = Environment.TickCount64 + 800;
            while (Environment.TickCount64 < deadline && got < buf.Length)
            {
                try { got += _sp.Read(buf, got, buf.Length - got); }
                catch (TimeoutException) { }

                for (int q = 0; q + 5 <= got; q++)
                {
                    if (buf[q] != SYNC) continue;
                    int len = buf[q + 1] | (buf[q + 2] << 8);
                    if (q + 5 + len + 2 > got) break;
                    if (buf[q + 3] == MSG_RSP)
                    {
                        return JsonSerializer.Deserialize<Dictionary<string, object?>>(
                            buf.AsSpan(q + 5, len)) ?? new() { ["ok"] = false };
                    }
                }
            }
            return new() { ["ok"] = false, ["error"] = "timeout" };
        }
    }

    static string ToJson(object o) => JsonSerializer.Serialize(o);
}
