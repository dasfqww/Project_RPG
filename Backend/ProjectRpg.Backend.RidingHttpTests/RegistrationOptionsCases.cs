using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Nodes;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Riding;

static partial class Program
{
    const string RegistrationOptionsPath = "/registration-options";
    static async Task RegistrationOptionsCases(bool pg)
    {
        await using var f = await Fixture.Create(pg);
        await Case("options_private_definition_contract", async () =>
        {
            var p = await f.New(); var result = await f.Get(p, p.Root + RegistrationOptionsPath); var c = result.AsObject();
            Check(c.Count == 9 && c["protocolVersion"]!.GetValue<int>() == 1, "Versioned exact root contract");
            var collection = await f.Get(p);
            Check(c["characterId"]!.GetValue<Guid>() == p.Character.CharacterId && JsonNode.DeepEquals(c["rosterId"], collection["rosterId"]), "Correlated owned character/roster");
            Check(c["rosterRevision"]!.GetValueKind() == JsonValueKind.String && c["rosterRevision"]!.GetValue<string>() == "0", "Revision remains an int64 string");
            Check(c["catalogReady"]!.GetValue<bool>() && c["registrationEnabled"]!.GetValue<bool>() && c["catalogVersion"]!.GetValue<int>() == 7
                && JsonNode.DeepEquals(c["gameplayHash"], collection["gameplayHash"]), "Same frozen catalog identity as collection");
            var option = c["options"]!.AsArray().Single()!.AsObject();
            Check(option.Count == 8 && option["definitionType"]!.GetValue<string>() == "RPGItemDefinition"
                && option["definitionName"]!.GetValue<string>() == p.Item.DefinitionName && option["definitionVersion"]!.GetValue<int>() == 1, "Full versioned item identity, not a display label");
            Check(option["mountId"]!.GetValue<string>() == "horse" && option["mountDefinitionVersion"]!.GetValue<int>() == 1
                && option["requiredQuantity"]!.GetValue<int>() == 1 && option["maximumStackSize"]!.GetValue<int>() == 99
                && !option["alreadyOwned"]!.GetValue<bool>(), "Normal registration requirement and current ownership observation");
            string body = c.ToJsonString();
            Check(!body.Contains(p.Token, StringComparison.Ordinal) && !body.Contains(p.Account, StringComparison.Ordinal)
                && !body.Contains(p.Item.ItemId.ToString("D"), StringComparison.Ordinal)
                && !body.Contains("rideSpeed", StringComparison.Ordinal) && !body.Contains("canRegister", StringComparison.Ordinal), "No token/account/item instances/gameplay authority or immediate write permission");
        });
        await Case("options_authentication_and_character_isolation", async () =>
        {
            var p = await f.New(); var other = await f.New(); string path = p.Root + RegistrationOptionsPath;
            await f.Raw(null, path, null, 401); await f.Raw("invalid-player-token", path, null, 401); await f.Raw(f.Admin, path, null, 403);
            await f.Raw(other.Token, path, null, 403);
            await f.Raw(p.Token, "/api/characters/" + Guid.NewGuid().ToString("D") + "/riding" + RegistrationOptionsPath, null, 403);
            await f.Raw(p.Token, path + "?catalogVersion=7", null, 400);
            var own = await f.Get(p, path); Check(own["options"]!.AsArray().Count == 1, "Denied reads never change authorized catalog");
            var now = DateTimeOffset.UtcNow;
            var session = await f.Game.CreateDungeonSessionAsync(p.Account, p.Character.CharacterId, "fixture", "Normal", now, now.AddHours(1), default);
            var server = new AccessTokenService(TimeProvider.System).Issue(TimeSpan.FromHours(1));
            await f.Game.AssignDungeonServerAsync(session.DungeonSessionId, "options-server", "127.0.0.1:1", now, now.AddHours(1), default);
            await f.Game.StoreGameServerCredentialAsync(server.TokenHash, "options-server", session.DungeonSessionId, server.ExpiresAt, default);
            await f.Raw(server.Token, path, null, 403);
        });
        await Case("options_read_is_not_write_permission", async () =>
        {
            var p = await f.New(); var before = await f.Items.FindAsync(p.Item.ItemId, default);
            var now = DateTimeOffset.UtcNow;
            await f.Game.CreateDungeonSessionAsync(p.Account, p.Character.CharacterId, "fixture", "Normal", now, now.AddHours(1), default);
            var body = await f.Get(p, p.Root + RegistrationOptionsPath);
            Check(body["registrationEnabled"]!.GetValue<bool>() && body["options"]!.AsArray().Count == 1, "Catalog inspection remains available during an active lease");
            var denied = await f.Post(p, p.Command(Guid.NewGuid()), 409);
            Check(denied["error"]!.GetValue<string>() == "RequiresLobby", "POST still checks actual current lobby/lease");
            var after = await f.Items.FindAsync(p.Item.ItemId, default);
            Check(JsonSerializer.Serialize(before, Json) == JsonSerializer.Serialize(after, Json), "Read/denial consume no item");
        });
        await Case("options_current_ownership_shared_roster_and_no_writes", async () =>
        {
            var p = await f.New(); var alt = await f.New(p.Account);
            var id = Guid.NewGuid(); await f.Post(p, p.Command(id));
            var beforeItem = await f.Items.FindAsync(p.Item.ItemId, default); var beforeCollection = await f.Get(p);
            var store = (IRidingPreferencesStore)f.Game;
            var beforeReceipt = await store.GetRidingCommandReceiptAsync(p.Account, p.Character.CharacterId, id, default);
            var body = await f.Get(p, p.Root + RegistrationOptionsPath); var altBody = await f.Get(alt, alt.Root + RegistrationOptionsPath);
            Check(body["options"]![0]!["alreadyOwned"]!.GetValue<bool>() && altBody["options"]![0]!["alreadyOwned"]!.GetValue<bool>(), "Same roster ownership observed for both characters");
            Check(body["rosterRevision"]!.GetValue<string>() == "1" && JsonNode.DeepEquals(body["rosterId"], altBody["rosterId"]), "Roster revision and identity match current store");
            body["options"]![0]!["maximumStackSize"] = 1; body["options"]![0]!["alreadyOwned"] = false;
            var fresh = await f.Get(p, p.Root + RegistrationOptionsPath);
            Check(fresh["options"]![0]!["maximumStackSize"]!.GetValue<int>() == 99 && fresh["options"]![0]!["alreadyOwned"]!.GetValue<bool>(), "Wire response mutation cannot change catalog or ownership");
            Check(JsonNode.DeepEquals(beforeCollection, await f.Get(p)), "No collection/preferences changes");
            Check(JsonSerializer.Serialize(beforeItem, Json) == JsonSerializer.Serialize(await f.Items.FindAsync(p.Item.ItemId, default), Json), "No inventory changes");
            Check(JsonSerializer.Serialize(beforeReceipt, Json) == JsonSerializer.Serialize(await store.GetRidingCommandReceiptAsync(p.Account, p.Character.CharacterId, id, default), Json), "Original receipt unchanged");
        });
        await Case("options_disabled_still_authorizes_owner", async () =>
        {
            var p = await f.New(); var foreign = await f.New(); await f.Post(p, p.Command(Guid.NewGuid()));
            await using var off = await Fixture.Create(pg, new RidingOptions(), f);
            var body = await off.Get(p, p.Root + RegistrationOptionsPath);
            Check(!body["catalogReady"]!.GetValue<bool>() && !body["registrationEnabled"]!.GetValue<bool>() && body["catalogVersion"] is null
                && body["gameplayHash"] is null && body["options"]!.AsArray().Count == 0 && body["rosterRevision"]!.GetValue<string>() == "1", "Disabled catalog yields empty options without erasing current roster");
            await off.Raw(foreign.Token, p.Root + RegistrationOptionsPath, null, 403);
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.State.Quantity == 1, "Disabled query does not refund or consume items");
        });
        await Case("options_sorted_enabled_mapping_and_startup_snapshot", async () =>
        {
            var options = Options(); options.Mounts = [new() { MountId = "horse", DefinitionVersion = 2 }, new() { MountId = "wolf", DefinitionVersion = 3, Enabled = false }];
            options.RegistrationItems = [
                new() { DefinitionType = "RPGItemDefinition", DefinitionName = "ZebraToken", DefinitionVersion = 1, MountId = "horse", MaximumStackSize = 10 },
                new() { DefinitionType = "RPGItemDefinition", DefinitionName = "AlphaToken", DefinitionVersion = 2, MountId = "horse", MaximumStackSize = 20 },
                new() { DefinitionType = "RPGItemDefinition", DefinitionName = "DisabledWolf", DefinitionVersion = 1, MountId = "wolf", MaximumStackSize = 1 },
                new() { DefinitionType = "RPGItemDefinition", DefinitionName = "AlphaToken", DefinitionVersion = 1, MountId = "horse", MaximumStackSize = 5 }];
            options.GameplayHash = new RidingCatalog(1, 7, options.Policy, options.Mounts.Select(m => new RidingMountDefinition(m.MountId, m.DefinitionVersion, m.Enabled, m.RegionTags)).ToArray(),
                options.RegistrationItems.Select(i => new RidingRegistrationDefinition(new(i.DefinitionType, i.DefinitionName, i.DefinitionVersion), i.MountId, i.MaximumStackSize, i.Uses)).ToArray()).GameplayHash;
            await using var custom = await Fixture.Create(pg, options, f); var p = await f.New();
            var body = await custom.Get(p, p.Root + RegistrationOptionsPath); var rows = body["options"]!.AsArray();
            Check(rows.Count == 3 && rows[0]!["definitionName"]!.GetValue<string>() == "AlphaToken" && rows[0]!["definitionVersion"]!.GetValue<int>() == 1
                && rows[1]!["definitionVersion"]!.GetValue<int>() == 2 && rows[2]!["definitionName"]!.GetValue<string>() == "ZebraToken", "Ordinal type/name/version order and disabled mount mapping hidden");
            Check(rows.All(r => r!["mountDefinitionVersion"]!.GetValue<int>() == 2), "Current enabled mount version");
            options.Mounts[0].Enabled = false; options.RegistrationItems[0].MaximumStackSize = 999;
            Check(JsonNode.DeepEquals(body, await custom.Get(p, p.Root + RegistrationOptionsPath)), "Mutable configuration is never reread after startup");
        });
        await Case("options_existing_read_budget_and_separate_write_budget", async () =>
        {
            var p = await f.New(); var otherCharacter = await f.New(p.Account); int denied = 0;
            for (int i = 0; i < 20; ++i)
            {
                using var request = new HttpRequestMessage(HttpMethod.Get, (i % 2 == 0 ? p.Root : otherCharacter.Root) + RegistrationOptionsPath);
                request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", p.Token);
                using var result = await f.Client.SendAsync(request);
                Check(result.Headers.CacheControl?.NoStore == true, "All GET outcomes private");
                if ((int)result.StatusCode == 429) { ++denied; Check(result.Headers.RetryAfter?.Delta == TimeSpan.FromSeconds(1), "Shared account read Retry-After"); }
                else Check(result.IsSuccessStatusCode, "Only read budget may deny valid owned query");
            }
            Check(denied > 0, "Character changes cannot bypass account read budget");
            await f.Post(p, p.Command(Guid.NewGuid()));
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.State.Quantity == 1, "Read budget exhaustion did not consume write budget or block legitimate registration");
        });
    }
}
