using System.Text.Json;
using System.Text.Json.Nodes;
using ProjectRpg.Backend.Configuration;

static partial class Program
{
    // Private loopback test host. Credentials and fixture are removed with the caller-owned run directory.
    static async Task<int> OwnerReadHost(string directory)
    {
        var run = Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_OWNER_RUN");
        if (!Guid.TryParseExact(run, "N", out _) || !Path.IsPathFullyQualified(directory)) throw new ArgumentException("Private run required");
        Directory.CreateDirectory(directory);
        await using var enabled = await Fixture.Create(false);
        var owner = await enabled.New(); var empty = await enabled.New(owner.Account); var foreign = await enabled.New(); var unowned = await enabled.New();
        var registrationCommand = owner.Command(Guid.NewGuid()); var preferencesCommand = PreferenceCommand(Guid.NewGuid());
        await enabled.Post(owner, registrationCommand);
        await Put(enabled, owner, preferencesCommand);
        await using var disabled = await Fixture.Create(false, new RidingOptions { Enabled = false }, enabled);
        var body = await enabled.Get(owner); var emptyBody = await enabled.Get(empty); var disabledBody = await disabled.Get(owner); var unownedBody = await enabled.Get(unowned);
        await File.WriteAllTextAsync(Path.Combine(directory, "private.json"), JsonSerializer.Serialize(new
        {
            run, endpoint = enabled.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api",
            disabledEndpoint = disabled.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api",
            owner.Account, owner.Token, enabled.Admin, character = owner.Character.CharacterId,
            registrationBody = JsonSerializer.Serialize(registrationCommand, Json), preferencesBody = JsonSerializer.Serialize(preferencesCommand, Json),
            emptyCharacter = empty.Character.CharacterId, foreignCharacter = foreign.Character.CharacterId,
            unownedCharacter = unowned.Character.CharacterId, unownedAccount = unowned.Account, unownedToken = unowned.Token,
            body = body.ToJsonString(), emptyBody = emptyBody.ToJsonString(), disabledBody = disabledBody.ToJsonString(), unownedBody = unownedBody.ToJsonString()
        }, Json));
        var deadline = DateTime.UtcNow.AddMinutes(10);
        while (!File.Exists(Path.Combine(directory, "stop")))
        { if (DateTime.UtcNow >= deadline) throw new TimeoutException("Private owner read host expired"); await Task.Delay(50); }
        bool unchanged = JsonNode.DeepEquals(body, await enabled.Get(owner)) && JsonNode.DeepEquals(emptyBody, await enabled.Get(empty))
            && JsonNode.DeepEquals(disabledBody, await disabled.Get(owner));
        Check(unchanged, "Read adapters preserve collection/preferences/item registration state");
        await File.WriteAllTextAsync(Path.Combine(directory, "stats.json"), JsonSerializer.Serialize(new { unchanged, Assertions = assertions }, Json));
        return 0;
    }
}
