using System.Net.Http.Headers;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;

static partial class Program
{
    static async Task<int> PreferencesUiHost(string directory)
    {
        var run = Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_OWNER_RUN");
        if (!Guid.TryParseExact(run, "N", out _) || !Path.IsPathFullyQualified(directory)) throw new ArgumentException("Private run required");
        Directory.CreateDirectory(directory); Person? actor = null, preferencesActor = null;
        int writes = 0, lost = 0, collections = 0, definitions = 0, inventories = 0, receipts = 0;
        int preferenceWrites = 0, preferenceLost = 0, preferenceReceipts = 0, preferenceCollections = 0, preferenceInventories = 0;
        await using var f = await Fixture.Create(false, includeItems: true, configure: app => app.Use(async (c, next) =>
        {
            if (actor is null) { await next(c); return; }
            if (preferencesActor is not null && ((c.Request.Path.Value?.StartsWith(preferencesActor.Root, StringComparison.Ordinal) == true)
                || (c.Request.Path == "/api/items" && c.Request.Query["ownerId"] == preferencesActor.Character.CharacterId.ToString("D"))))
            {
                if (c.Request.Method == "GET")
                {
                    if (c.Request.Path == preferencesActor.Root) ++preferenceCollections;
                    if (c.Request.Path.StartsWithSegments(preferencesActor.Root + "/requests")) ++preferenceReceipts;
                    if (c.Request.Path == "/api/items") ++preferenceInventories;
                    await next(c); return;
                }
                ++preferenceWrites;
                var originalPreferences = c.Response.Body; await using var preferenceBuffer = new MemoryStream(); c.Response.Body = preferenceBuffer;
                await next(c); c.Response.Body = originalPreferences;
                if (c.Response.StatusCode == 200 && preferenceLost == 0)
                { ++preferenceLost; c.Response.StatusCode = 503; c.Response.ContentLength = null; c.Response.Headers.RetryAfter = "1"; await c.Response.WriteAsync("{\"error\":\"StorageUnavailable\"}"); }
                else { preferenceBuffer.Position = 0; await preferenceBuffer.CopyToAsync(originalPreferences); }
                return;
            }
            if (c.Request.Method == "GET")
            {
                if (c.Request.Path == actor.Root) ++collections;
                if (c.Request.Path == actor.Root + RegistrationOptionsPath) ++definitions;
                if (c.Request.Path.StartsWithSegments(actor.Root + "/requests")) ++receipts;
                if (c.Request.Path == "/api/items" && c.Request.Query["ownerId"] == actor.Character.CharacterId.ToString("D")) ++inventories;
                await next(c); return;
            }
            ++writes;
            if (c.Request.Path != actor.Root + "/register") { await next(c); return; }
            var original = c.Response.Body; await using var buffer = new MemoryStream(); c.Response.Body = buffer;
            await next(c); c.Response.Body = original;
            if (c.Response.StatusCode == 200 && lost == 0)
            { ++lost; c.Response.StatusCode = 503; c.Response.ContentLength = null; c.Response.Headers.RetryAfter = "1"; await c.Response.WriteAsync("{\"error\":\"StorageUnavailable\"}"); }
            else { buffer.Position = 0; await buffer.CopyToAsync(original); }
        }));
        var owner = await f.New(); var empty = await f.New(owner.Account); var foreign = await f.New(); var unowned = await f.New();
        await f.Post(owner, owner.Command(Guid.NewGuid())); await Put(f, owner, PreferenceCommand(Guid.NewGuid()));
        await using var disabled = await Fixture.Create(false, new ProjectRpg.Backend.Configuration.RidingOptions(), f);
        var body = await f.Get(owner); var emptyBody = await f.Get(empty); var unownedBody = await f.Get(unowned); var disabledBody = await disabled.Get(owner);
        var options = await f.Get(unowned, unowned.Root + RegistrationOptionsPath);
        async Task<string> Items(Person p)
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, "/api/items?ownerType=Character&ownerId=" + p.Character.CharacterId.ToString("D") + "&includeTerminal=false&limit=500");
            request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", p.Token);
            using var response = await f.Client.SendAsync(request); response.EnsureSuccessStatusCode(); return await response.Content.ReadAsStringAsync();
        }
        var candidateItems = await Items(unowned);
        var preferences = await f.New(); await f.Post(preferences, preferences.Command(Guid.NewGuid()));
        var preferenceBody = await f.Get(preferences); var preferenceItems = await Items(preferences);
        actor = await f.New(); preferencesActor = preferences;
        await File.WriteAllTextAsync(Path.Combine(directory, "private.json"), JsonSerializer.Serialize(new
        {
            run, endpoint = f.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api", owner.Account, owner.Token, character = owner.Character.CharacterId,
            disabledEndpoint = disabled.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api", f.Admin,
            emptyCharacter = empty.Character.CharacterId, foreignCharacter = foreign.Character.CharacterId,
            unownedCharacter = unowned.Character.CharacterId, unownedAccount = unowned.Account, unownedToken = unowned.Token,
            body = body.ToJsonString(), emptyBody = emptyBody.ToJsonString(), unownedBody = unownedBody.ToJsonString(),
            disabledBody = disabledBody.ToJsonString(),
            candidateOptions = options.ToJsonString(), candidateItems, candidateItem = unowned.Item.ItemId,
            liveCharacter = actor.Character.CharacterId, liveAccount = actor.Account, liveToken = actor.Token, liveItem = actor.Item.ItemId,
            prefsCharacter = preferences.Character.CharacterId, prefsAccount = preferences.Account, prefsToken = preferences.Token,
            prefsItem = preferences.Item.ItemId, prefsBody = preferenceBody.ToJsonString(), prefsItems = preferenceItems
        }, Json));
        var deadline = DateTime.UtcNow.AddMinutes(15);
        while (!File.Exists(Path.Combine(directory, "stop"))) { if (DateTime.UtcNow >= deadline) throw new TimeoutException(); await Task.Delay(50); }
        var item = (await f.Items.FindAsync(actor.Item.ItemId, default))!; var current = await f.Get(actor);
        bool passed = writes == 1 && lost == 1 && receipts == 1 && collections >= 2 && definitions == 1 && inventories == 2
            && item.Revision == 2 && item.State.Quantity == 1 && current["mounts"]!.AsArray().Count == 1
            && (await f.Items.FindAsync(unowned.Item.ItemId, default))!.State.Quantity == 2;
        var storedPreferences = await f.Get(preferences); var retainedItem = (await f.Items.FindAsync(preferences.Item.ItemId, default))!;
        bool preferencesPassed = preferenceWrites == 1 && preferenceLost == 1 && preferenceReceipts == 1 && preferenceCollections >= 2 && preferenceInventories == 1
            && storedPreferences["preferencesRevision"]!.GetValue<string>() == "1"
            && storedPreferences["preferences"]!["selectedMountId"]!.GetValue<string>() == "horse"
            && storedPreferences["preferences"]!["favoriteMountIds"]!.AsArray().Count == 1
            && storedPreferences["preferences"]!["shortcut"]!["mode"]!.GetValue<string>() == "RandomFavorites"
            && retainedItem.Revision == 2 && retainedItem.State.Quantity == 1;
        Check(passed && preferencesPassed, "Registration regression and explicit settings PUT commit once, recover original receipts and keep current assets");
        await File.WriteAllTextAsync(Path.Combine(directory, "stats.json"), JsonSerializer.Serialize(new { passed = passed && preferencesPassed, writes, lost, collections, definitions, inventories, receipts,
            preferenceWrites, preferenceLost, preferenceReceipts, preferenceCollections, preferenceInventories, preferenceRevision = "1", unchangedItemQuantity = retainedItem.State.Quantity }, Json));
        return 0;
    }
}
