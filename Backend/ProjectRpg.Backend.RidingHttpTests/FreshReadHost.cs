using System.Net.Http.Headers;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using ProjectRpg.Backend.Domain;

static partial class Program
{
    static async Task<int> FreshReadHost(string directory)
    {
        var run = Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_OWNER_RUN");
        if (!Guid.TryParseExact(run, "N", out _) || !Path.IsPathFullyQualified(directory)) throw new ArgumentException("Private run required");
        Directory.CreateDirectory(directory); Fixture? host = null; Person? actor = null; int writes = 0, collections = 0, inventories = 0;
        await using var f = await Fixture.Create(false, includeItems: true, configure: app => app.Use(async (c, next) =>
        {
            bool registration = actor is not null && c.Request.Path == actor.Root + "/register" && c.Request.Method == "POST";
            if (actor is not null && c.Request.Method == "GET")
            {
                if (c.Request.Path == actor.Root) ++collections;
                if (c.Request.Path == "/api/items" && c.Request.Query["ownerId"] == actor.Character.CharacterId.ToString("D")) ++inventories;
            }
            if (!registration) { await next(c); return; }
            var original = c.Response.Body; await using var buffer = new MemoryStream(); c.Response.Body = buffer;
            await next(c); c.Response.Body = original;
            if (c.Response.StatusCode == 200)
            {
            ++writes;
            // A later real item transaction makes the registration receipt's remaining stack historical.
            var current = (await host!.Items.FindAsync(actor!.Item.ItemId, default))!;
            Check(current.Revision == 2 && current.State.Quantity == 1, "Registration consumed one item");
            var changed = current with { State = current.State with { Quantity = 0 }, LifecycleState = ItemLifecycleState.Consumed, Location = new(ItemContainerType.Terminal, "", -1) };
            var result = await host.Items.CommitAsync(new(Guid.NewGuid(), "Test.FreshConsume", Guid.NewGuid().ToString("N"), current.Owner, 1, [new(current.Revision, changed)]), DateTimeOffset.UtcNow, default);
            Check(result.Status == ItemRepositoryCommitStatus.Committed, "Later consumption committed");
            }
            buffer.Position = 0; await buffer.CopyToAsync(original);
        }));
        host = f;
        var seed = await f.New(); var seedCommand = seed.Command(Guid.NewGuid()); var receipt = await f.Post(seed, seedCommand); var collection = await f.Get(seed);
        using var request = new HttpRequestMessage(HttpMethod.Get, "/api/items?ownerType=Character&ownerId=" + seed.Character.CharacterId.ToString("D") + "&includeTerminal=false&limit=500");
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", seed.Token);
        using var response = await f.Client.SendAsync(request); response.EnsureSuccessStatusCode(); var items = await response.Content.ReadAsStringAsync();
        actor = await f.New(); var command = actor.Command(Guid.NewGuid());
        await File.WriteAllTextAsync(Path.Combine(directory, "private.json"), JsonSerializer.Serialize(new
        {
            run, endpoint = f.Client.BaseAddress!.AbsoluteUri.TrimEnd('/') + "/api", actor.Account, actor.Token, character = actor.Character.CharacterId,
            registrationBody = JsonSerializer.Serialize(command, Json), seedCharacter = seed.Character.CharacterId, seedAccount = seed.Account,
            seedBody = JsonSerializer.Serialize(seedCommand, Json), receipt = receipt.ToJsonString(), collection = collection.ToJsonString(), items
        }, Json));
        var deadline = DateTime.UtcNow.AddMinutes(15);
        while (!File.Exists(Path.Combine(directory, "stop"))) { if (DateTime.UtcNow >= deadline) throw new TimeoutException(); await Task.Delay(50); }
        var item = (await f.Items.FindAsync(actor.Item.ItemId, default))!;
        bool passed = writes == 1 && collections == 1 && inventories == 1 && item.Revision == 3 && item.State.Quantity == 0 && item.LifecycleState == ItemLifecycleState.Consumed;
        Check(passed, "Fresh GETs, no repeated command, no resurrection from historical receipt");
        await File.WriteAllTextAsync(Path.Combine(directory, "stats.json"), JsonSerializer.Serialize(new { passed, writes, collections, inventories, itemRevision = item.Revision, quantity = item.State.Quantity }, Json));
        return 0;
    }
}
