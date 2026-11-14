using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Riding;

static partial class Program
{
    static async Task<int> RegistrationReadHost(string directory)
    {
        var run = Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_OWNER_RUN");
        if (!Guid.TryParseExact(run, "N", out _) || !Path.IsPathFullyQualified(directory)) throw new ArgumentException("Private run required");
        Directory.CreateDirectory(directory);
        int registrationReads = 0, writes = 0;
        await using var enabled = await Fixture.Create(false, configure: app => app.Use(async (c, next) =>
        {
            if (c.Request.Path.Value?.EndsWith("/registration-options", StringComparison.Ordinal) == true) Interlocked.Increment(ref registrationReads);
            if (c.Request.Method != "GET") Interlocked.Increment(ref writes);
            await next(c);
        }));
        var owner = await enabled.New(); var empty = await enabled.New(owner.Account); var foreign = await enabled.New(); var unowned = await enabled.New();
        await enabled.Post(owner, owner.Command(Guid.NewGuid())); await Put(enabled, owner, PreferenceCommand(Guid.NewGuid()));
        await using var disabled = await Fixture.Create(false, new RidingOptions(), enabled);
        var maximum = Options(); string mount = new('m', 64);
        maximum.Mounts = [new() { MountId = mount, DefinitionVersion = int.MaxValue }];
        maximum.RegistrationItems = Enumerable.Range(0, 512).Select(i => new RidingRegistrationOptions
        {
            DefinitionType = new('T', 64), DefinitionName = new string('N', 125) + i.ToString("D3"), DefinitionVersion = int.MaxValue,
            MountId = mount, MaximumStackSize = int.MaxValue
        }).ToArray();
        maximum.GameplayHash = new RidingCatalog(1, 7, maximum.Policy,
            [new(mount, int.MaxValue, true, [])], maximum.RegistrationItems.Select(i => new RidingRegistrationDefinition(
                new(i.DefinitionType, i.DefinitionName, i.DefinitionVersion), i.MountId, i.MaximumStackSize, i.Uses)).ToArray()).GameplayHash;
        await using var maximumHost = await Fixture.Create(false, maximum, enabled);
        var body = await enabled.Get(owner); var emptyBody = await enabled.Get(empty); var disabledBody = await disabled.Get(owner); var unownedBody = await enabled.Get(unowned);
        var item = JsonSerializer.Serialize(await enabled.Items.FindAsync(owner.Item.ItemId, default), Json);
        var options = await enabled.Get(owner, owner.Root + RegistrationOptionsPath);
        var emptyOptions = await enabled.Get(empty, empty.Root + RegistrationOptionsPath);
        var disabledOptions = await disabled.Get(owner, owner.Root + RegistrationOptionsPath);
        var unownedOptions = await enabled.Get(unowned, unowned.Root + RegistrationOptionsPath);
        var maximumOptions = await maximumHost.Get(owner, owner.Root + RegistrationOptionsPath);
        int initialWrites = writes;
        await File.WriteAllTextAsync(Path.Combine(directory, "private.json"), JsonSerializer.Serialize(new
        {
            run, endpoint = enabled.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api",
            disabledEndpoint = disabled.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api",
            maximumEndpoint = maximumHost.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api",
            owner.Account, owner.Token, enabled.Admin, character = owner.Character.CharacterId,
            emptyCharacter = empty.Character.CharacterId, foreignCharacter = foreign.Character.CharacterId,
            unownedCharacter = unowned.Character.CharacterId, unownedAccount = unowned.Account, unownedToken = unowned.Token,
            body = body.ToJsonString(), emptyBody = emptyBody.ToJsonString(), disabledBody = disabledBody.ToJsonString(), unownedBody = unownedBody.ToJsonString(),
            registrationOptionsBody = options.ToJsonString(), emptyOptionsBody = emptyOptions.ToJsonString(),
            disabledOptionsBody = disabledOptions.ToJsonString(), unownedOptionsBody = unownedOptions.ToJsonString(), maximumOptionsBody = maximumOptions.ToJsonString()
        }, Json));
        var deadline = DateTime.UtcNow.AddMinutes(10);
        while (!File.Exists(Path.Combine(directory, "stop")))
        { if (DateTime.UtcNow >= deadline) throw new TimeoutException("Private registration read host expired"); await Task.Delay(50); }
        bool unchanged = JsonNode.DeepEquals(body, await enabled.Get(owner)) && JsonNode.DeepEquals(emptyBody, await enabled.Get(empty))
            && JsonNode.DeepEquals(disabledBody, await disabled.Get(owner)) && item == JsonSerializer.Serialize(await enabled.Items.FindAsync(owner.Item.ItemId, default), Json)
            && initialWrites == writes;
        Check(unchanged, "UE reads preserve current collection/preferences/inventory and never send writes");
        await File.WriteAllTextAsync(Path.Combine(directory, "stats.json"), JsonSerializer.Serialize(new { unchanged, registrationReads, clientWrites = writes - initialWrites, Assertions = assertions }, Json));
        return 0;
    }
}
