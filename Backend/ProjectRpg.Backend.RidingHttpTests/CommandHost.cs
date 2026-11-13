using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;

static partial class Program
{
    static async Task<int> CommandHost(string directory)
    {
        var run = Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_OWNER_RUN");
        if (!Guid.TryParseExact(run, "N", out _) || !Path.IsPathFullyQualified(directory)) throw new ArgumentException("Private run required");
        Directory.CreateDirectory(directory); bool loseNext = false; var writes = new List<string>(); int lost = 0;
        await using var f = await Fixture.Create(false, configure: app => app.Use(async (c, next) =>
        {
            bool write = c.Request.Method is "POST" or "PUT";
            if (!write) { await next(c); return; }
            c.Request.EnableBuffering(); using (var reader = new StreamReader(c.Request.Body, Encoding.UTF8, leaveOpen: true)) writes.Add(await reader.ReadToEndAsync()); c.Request.Body.Position = 0;
            if (!loseNext) { await next(c); return; } loseNext = false;
            var original = c.Response.Body; await using var buffer = new MemoryStream(); c.Response.Body = buffer;
            await next(c); c.Response.Body = original;
            if (c.Response.StatusCode == 200) { ++lost; c.Response.StatusCode = 503; c.Response.ContentLength = null; await c.Response.WriteAsync("{\"error\":\"StorageUnavailable\"}"); }
            else { buffer.Position = 0; await buffer.CopyToAsync(original); }
        }));
        var seed = await f.New(); var seedRegister = seed.Command(Guid.NewGuid()); var seedPreferences = PreferenceCommand(Guid.NewGuid());
        var registration = await f.Post(seed, seedRegister); var preferences = await Put(f, seed, seedPreferences);
        var actor = await f.New(); var command = actor.Command(Guid.NewGuid()); var setting = PreferenceCommand(Guid.NewGuid());
        writes.Clear(); loseNext = true;
        await File.WriteAllTextAsync(Path.Combine(directory, "private.json"), JsonSerializer.Serialize(new
        {
            run, endpoint = f.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api", actor.Account, actor.Token, character = actor.Character.CharacterId,
            registrationBody = JsonSerializer.Serialize(command, Json), preferencesBody = JsonSerializer.Serialize(setting, Json),
            seedCharacter = seed.Character.CharacterId, seedRegistrationBody = JsonSerializer.Serialize(seedRegister, Json), seedPreferencesBody = JsonSerializer.Serialize(seedPreferences, Json),
            registration = registration.ToJsonString(), preferences = preferences.ToJsonString()
        }, Json));
        var deadline = DateTime.UtcNow.AddMinutes(10);
        while (!File.Exists(Path.Combine(directory, "stop"))) { if (DateTime.UtcNow >= deadline) throw new TimeoutException(); await Task.Delay(50); }
        var view = await f.Get(actor);
        bool passed = lost == 1 && writes.Count == 2 && writes[0] == JsonSerializer.Serialize(command, Json) && writes[1] == JsonSerializer.Serialize(setting, Json)
            && view["mounts"]!.AsArray().Count == 1 && view["rosterRevision"]!.GetValue<string>() == "1" && view["preferencesRevision"]!.GetValue<string>() == "1";
        Check(passed, "Committed 503 recovered, each original command dispatched once, no duplicate mutation");
        await File.WriteAllTextAsync(Path.Combine(directory, "stats.json"), JsonSerializer.Serialize(new { passed, lost, writes = writes.Count, rosterRevision = "1", preferencesRevision = "1" }, Json));
        return 0;
    }
}
