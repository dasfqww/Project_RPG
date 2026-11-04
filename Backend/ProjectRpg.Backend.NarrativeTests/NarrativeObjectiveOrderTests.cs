using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Narrative;

internal static class NarrativeObjectiveOrderTests
{
    public static async Task RunAsync()
    {
        using var stream = typeof(NarrativeCatalog).Assembly.GetManifestResourceStream("ProjectRpg.Backend.Narrative.Content.development.json")!;
        using var reader = new StreamReader(stream);
        var original = JsonNode.Parse(await reader.ReadToEndAsync())!.AsObject();
        static NarrativeCatalog Parse(JsonObject document) => NarrativeCatalogLoader.Parse(Encoding.UTF8.GetBytes(document.ToJsonString()));
        void Reject(string requirements, string expected)
        {
            var invalid = original.DeepClone().AsObject();
            invalid["quests"]![0]!["objectivePrerequisites"] = JsonNode.Parse(requirements);
            try { _ = Parse(invalid); }
            catch (NarrativeCatalogException error) when (error.Code == expected) { return; }
            throw new TestFailure("Objective prerequisites should reject: " + expected);
        }
        Reject("{\"missing\":[\"contact\"]}", "invalid_objective_prerequisites");
        Reject("{\"preparation\":[\"missing\"]}", "invalid_objective_prerequisites");
        Reject("{\"contact\":[\"contact\"]}", "invalid_objective_prerequisites");
        Reject("{\"preparation\":[\"contact\",\"contact\"]}", "invalid_objective_prerequisites");
        Reject("{\"contact\":[\"preparation\"],\"preparation\":[\"contact\"]}", "cyclic_objective_prerequisites");
        Reject("{\"preparation\":null}", "invalid_narrative_catalog_json");
        var ordered = original.DeepClone().AsObject();
        ordered["quests"]![0]!["objectivePrerequisites"] = JsonNode.Parse("{\"preparation\":[\"contact\"]}");
        var catalog = Parse(ordered); var definition = catalog.Quests["DEV.Introduction"];
        var quests = new QuestManager(); var dialogues = new DialogueManager(quests);
        var blank = NarrativeState.Create(Guid.NewGuid());
        var accepted = quests.Apply(blank, definition, new("accept", "Accept", DialogueEffect.AcceptQuest));
        Require(quests.CanRecordObjective(accepted.Quests[0], NarrativeCatalog.Development.Quests[definition.Id], "preparation"), "Omitted prerequisites changed parallel objectives.");
        Require(!quests.CanRecordObjective(accepted.Quests[0], definition, "preparation"), "Ordered objective opened early.");
        RejectRule(() => { _ = quests.Apply(accepted, definition, new("confirm", "Confirm", DialogueEffect.RecordObjective, "preparation")); }, "objective_prerequisite_not_met");
        var graph = original.DeepClone().AsObject();
        graph["quests"]![0]!["objectives"] = new JsonArray("contact", "preparation", "route", "finish");
        graph["quests"]![0]!["objectivePrerequisites"] = JsonNode.Parse("{\"route\":[\"contact\",\"preparation\"],\"finish\":[\"route\"]}");
        foreach (string id in new[] { "route", "finish" })
        {
            var node = graph["dialogues"]![1]!.DeepClone(); node["id"] = "DEV." + id;
            node["choices"]![0]!["objectiveId"] = id; graph["dialogues"]!.AsArray().Add(node);
        }
        var diamond = Parse(graph); var combined = diamond.Quests[definition.Id];
        var current = quests.Apply(blank, combined, new("accept", "Accept", DialogueEffect.AcceptQuest));
        foreach (string id in new[] { "contact", "preparation", "route", "finish" })
        {
            if (id == "preparation") Require(!quests.CanRecordObjective(current.Quests[0], combined, "route"), "AND gate opened with one prerequisite.");
            Require(quests.CanRecordObjective(current.Quests[0], combined, id), "DAG objective did not become available: " + id);
            current = quests.Apply(current, combined, new("confirm", "Confirm", DialogueEffect.RecordObjective, id));
        }
        Require(current.Quests[0].Status == QuestStatus.ReadyToReport, "DAG did not reach report after all objectives.");

        var transaction = new InMemoryTransactionGate(); var game = new InMemoryGameRepository(transaction, new InMemoryItemRepository(transaction));
        const string owner = "76561198000000001";
        await game.UpsertAccountAsync(new(owner, owner, false, false), DateTimeOffset.UtcNow, default);
        var character = await game.CreateCharacterAsync(owner, "OrderMain", DateTimeOffset.UtcNow, default);
        var alt = await game.CreateCharacterAsync(owner, "OrderAlt", DateTimeOffset.UtcNow, default);
        NarrativeCoordinator Flow() => new(game, catalog, quests, new ScenarioManager(), dialogues, TimeProvider.System);
        string growth = JsonSerializer.Serialize(await game.LoadProgressionAsync(character.CharacterId, DateTimeOffset.UtcNow, default));
        var tokens = new AccessTokenService(TimeProvider.System); var token = tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreSessionAsync(token.TokenHash, owner, token.ExpiresAt, default);
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IGameRepository>(game); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
        builder.Services.AddSingleton(Flow()); builder.Services.AddScoped<BearerAuthenticator>();
        builder.Services.Configure<AuthOptions>(_ => { }); builder.Services.Configure<SecurityTelemetryOptions>(_ => { });
        await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi(); await app.StartAsync();
        try
        {
            using var client = new HttpClient { BaseAddress = new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token.Token);
            string route = $"/api/characters/{character.CharacterId}/narrative";
            object Command(Guid id, int revision, string node, string choice) => new { requestId = id, expectedRevision = revision.ToString(), catalogVersion = 1, dialogueId = "DEV." + node, choiceId = choice };
            Require((await client.PostAsJsonAsync(route + "/choices", Command(Guid.NewGuid(), 0, "Contact.Offer", "accept"))).IsSuccessStatusCode, "Accept failed.");
            Require(!(await client.GetStringAsync(route)).Contains("DEV.Instructor.Explain", StringComparison.Ordinal), "Blocked objective node leaked in available dialogues.");
            Guid request = Guid.NewGuid();
            var denied = await client.PostAsJsonAsync(route + "/choices", Command(request, 1, "Instructor.Explain", "confirm"));
            Require(denied.StatusCode == HttpStatusCode.Conflict && (await denied.Content.ReadAsStringAsync()).Contains("dialogue_choice_not_available"), "Forged out-of-order choice allowed.");
            Require((await Flow().LoadAsync(character.CharacterId, owner, default)).State.Revision == 1, "Rejected objective advanced revision.");
            var legal = Command(request, 1, "Contact.Explain", "confirm");
            Require((await client.PostAsJsonAsync(route + "/choices", legal)).IsSuccessStatusCode, "Rejected request left a receipt.");
            Require((await client.PostAsJsonAsync(route + "/choices", legal)).IsSuccessStatusCode, "Exact objective replay failed.");
            Require((await Flow().LoadAsync(character.CharacterId, owner, default)).State.Revision == 2, "Replay duplicated objective progress.");
            string body = await client.GetStringAsync(route);
            Require(body.Contains("DEV.Instructor.Explain", StringComparison.Ordinal) && !body.Contains("DEV.Contact.Explain", StringComparison.Ordinal), "Unlocked/completed choices filtered incorrectly.");
            denied = await client.PostAsJsonAsync(route + "/choices", Command(Guid.NewGuid(), 1, "Instructor.Explain", "confirm"));
            Require(denied.StatusCode == HttpStatusCode.Conflict && (await denied.Content.ReadAsStringAsync()).Contains("narrative_revision_conflict"), "Stale objective revision allowed.");
            Require((await client.PostAsJsonAsync(route + "/choices", Command(Guid.NewGuid(), 2, "Instructor.Explain", "confirm"))).IsSuccessStatusCode, "Unlocked objective failed.");
            Require((await Flow().LoadAsync(character.CharacterId, owner, default)).State.Quests[0].Status == QuestStatus.ReadyToReport, "All objectives did not reach report.");
            Require((await client.PostAsJsonAsync(route + "/choices", Command(Guid.NewGuid(), 3, "Contact.Report", "report"))).IsSuccessStatusCode, "Ordered quest report failed.");
            var restored = await Flow().LoadAsync(character.CharacterId, owner, default);
            Require(restored.State.Revision == 4 && restored.State.CompletedChapters.Contains("DEV.Prologue"), "Ordered quest completion did not restore.");
            Require((await Flow().LoadAsync(alt.CharacterId, owner, default)).State.Quests.IsEmpty, "Objective progress leaked to alt.");
        }
        finally { await app.StopAsync(); }
        await game.CommitNarrativeAsync(alt.CharacterId, owner, new(Guid.NewGuid(), 0, 1, "test", "test"), state =>
            state with { Revision = 1, Quests = [new(definition.Id, definition.Version, QuestStatus.Active, ["preparation"])] }, DateTimeOffset.UtcNow, default);
        await RejectState(async () => { _ = await Flow().LoadAsync(alt.CharacterId, owner, default); }, "incompatible_narrative_state");
        Require(growth == JsonSerializer.Serialize(await game.LoadProgressionAsync(character.CharacterId, DateTimeOffset.UtcNow, default)), "Objective sequencing changed growth.");
        Console.WriteLine("PASS Narrative objective order: references/duplicates/self/cycles/null, DAG/AND and legacy parallel rules, HTTP filtering/forged choice/no receipt/replay/stale revision, report/restore, corrupt save and alt/growth isolation.");
    }

    private static void RejectRule(Action action, string code)
    {
        try { action(); }
        catch (NarrativeConflictException error) when (error.Message == code) { return; }
        throw new TestFailure("Expected objective rejection: " + code);
    }
    private static async Task RejectState(Func<Task> action, string code)
    {
        try { await action(); }
        catch (NarrativeConflictException error) when (error.Message == code) { return; }
        throw new TestFailure("Expected stored objective rejection: " + code);
    }
    private static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
