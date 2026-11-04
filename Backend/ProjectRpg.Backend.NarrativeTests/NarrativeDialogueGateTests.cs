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

internal static class NarrativeDialogueGateTests
{
    public static async Task RunAsync()
    {
        static async Task<JsonObject> Read(Stream stream)
        { using (stream) { using var reader = new StreamReader(stream); return JsonNode.Parse(await reader.ReadToEndAsync())!.AsObject(); } }
        var document = await Read(typeof(NarrativeCatalog).Assembly.GetManifestResourceStream("ProjectRpg.Backend.Narrative.Content.development.json")!);
        var information = await Read(typeof(NarrativeDialogueGateTests).Assembly.GetManifestResourceStream("NarrativeInformationFixture")!);
        foreach (var node in information["dialogues"]!.AsArray())
        {
            var copy = node!.DeepClone(); copy["requiredCompletedQuests"] = new JsonArray("DEV.Introduction");
            document["dialogues"]!.AsArray().Add(copy);
        }
        NarrativeCatalog Parse(JsonObject json) => NarrativeCatalogLoader.Parse(Encoding.UTF8.GetBytes(json.ToJsonString()));
        void Reject(Action<JsonObject> edit, string code)
        {
            var invalid = document.DeepClone().AsObject(); edit(invalid);
            try { _ = Parse(invalid); }
            catch (NarrativeCatalogException error) when (error.Code == code) { return; }
            throw new TestFailure("Dialogue gate should reject: " + code);
        }
        Reject(j => j["dialogues"]![4]!["requiredCompletedQuests"] = new JsonArray("missing"), "invalid_dialogue_requirements");
        Reject(j => j["dialogues"]![4]!["requiredCompletedQuests"] = new JsonArray("DEV.Introduction", "DEV.Introduction"), "invalid_dialogue_requirements");
        Reject(j => j["dialogues"]![5]!["requiredCompletedQuests"] = new JsonArray(), "inconsistent_information_requirements");
        Reject(j => j["dialogues"]![0]!["requiredCompletedQuests"] = new JsonArray("DEV.Introduction"), "invalid_dialogue_requirements");
        var catalog = Parse(document);
        var quest = catalog.Quests["DEV.Introduction"];
        var second = quest with { Id = "DEV.Second" };
        var two = new NarrativeCatalog(1, [quest, second], catalog.Chapters.Values,
            catalog.Dialogues.Values.Select(d => d.Informational ? d with { RequiredCompletedQuests = [quest.Id, second.Id] } : d)
                .Concat(catalog.Dialogues.Values.Where(d => !d.Informational).Select(d => d with { Id = "SECOND." + d.Id, QuestId = second.Id })));
        var state = NarrativeState.Create(Guid.NewGuid()) with { Quests = [new(quest.Id, 1, QuestStatus.Completed, quest.Objectives), new(second.Id, 1, QuestStatus.Active, [])] };
        var dialogueManager = new DialogueManager(new QuestManager());
        Require(!dialogueManager.GetAvailable(state, two).Any(d => d.Informational), "AND gate opened with one requirement missing.");
        state = state with { Quests = [state.Quests[0], new(second.Id, 1, QuestStatus.Completed, second.Objectives)] };
        Require(dialogueManager.GetAvailable(state, two).Count(d => d.Informational) == 3, "AND gate failed after all requirements completed.");

        var gate = new InMemoryTransactionGate(); var game = new InMemoryGameRepository(gate, new InMemoryItemRepository(gate));
        const string owner = "76561198000000001";
        await game.UpsertAccountAsync(new(owner, owner, false, false), DateTimeOffset.UtcNow, default);
        var character = await game.CreateCharacterAsync(owner, "GateTest", DateTimeOffset.UtcNow, default);
        var alt = await game.CreateCharacterAsync(owner, "GateAlt", DateTimeOffset.UtcNow, default);
        NarrativeCoordinator Flow() => new(game, catalog, new QuestManager(), new ScenarioManager(), dialogueManager, TimeProvider.System);
        var flow = Flow(); var tokens = new AccessTokenService(TimeProvider.System); var token = tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreSessionAsync(token.TokenHash, owner, token.ExpiresAt, default);
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IGameRepository>(game); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
        builder.Services.AddScoped<BearerAuthenticator>(); builder.Services.Configure<AuthOptions>(_ => { }); builder.Services.Configure<SecurityTelemetryOptions>(_ => { });
        builder.Services.AddSingleton(flow);
        await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi(); await app.StartAsync();
        try
        {
            using var client = new HttpClient { BaseAddress = new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token.Token);
            string route = $"/api/characters/{character.CharacterId}/narrative";
            async Task Hidden()
            { Require(!(await client.GetStringAsync(route)).Contains("INFO.", StringComparison.Ordinal), "Locked node or continuation leaked in HTTP snapshot."); }
            await Hidden(); Guid request = Guid.NewGuid();
            var denied = await client.PostAsJsonAsync(route + "/choices", new { requestId = request, expectedRevision = "0", catalogVersion = 1, dialogueId = "INFO.Start", choiceId = "ask" });
            Require(denied.StatusCode == HttpStatusCode.Conflict && (await denied.Content.ReadAsStringAsync()).Contains("dialogue_not_available"), "Locked forged choice allowed.");
            await flow.ChooseAsync(character.CharacterId, owner, new(request, 0, 1, "DEV.Contact.Offer", "accept"), default);
            await Hidden();
            await flow.ChooseAsync(character.CharacterId, owner, new(Guid.NewGuid(), 1, 1, "DEV.Contact.Explain", "confirm"), default);
            await flow.ChooseAsync(character.CharacterId, owner, new(Guid.NewGuid(), 2, 1, "DEV.Instructor.Explain", "confirm"), default);
            await Hidden(); // ReadyToReport is not Completed.
            await flow.ChooseAsync(character.CharacterId, owner, new(Guid.NewGuid(), 3, 1, "DEV.Contact.Report", "report"), default);
            using var snapshot = JsonDocument.Parse(await client.GetStringAsync(route));
            Require(snapshot.RootElement.GetProperty("availableDialogues").GetArrayLength() == 3, "Completed quest did not expose the full information graph.");
            Require((await Flow().LoadAsync(character.CharacterId, owner, default)).AvailableDialogues.Count(d => d.Informational) == 3, "New coordinator lost completed qualification.");
            Require(!(await Flow().LoadAsync(alt.CharacterId, owner, default)).AvailableDialogues.Any(d => d.Informational), "Qualification leaked to an alt.");
            denied = await client.PostAsJsonAsync(route + "/choices", new { requestId = Guid.NewGuid(), expectedRevision = "4", catalogVersion = 1, dialogueId = "INFO.Start", choiceId = "ask" });
            Require(denied.StatusCode == HttpStatusCode.Conflict && (await denied.Content.ReadAsStringAsync()).Contains("informational_dialogue_not_mutable"), "Visible information became mutable.");
            Require((await flow.LoadAsync(character.CharacterId, owner, default)).State.Revision == 4, "Information gate query advanced progress.");
        }
        finally { await app.StopAsync(); }
        Console.WriteLine("PASS Narrative dialogue gates: references/duplicates/graph consistency, AND requirements, HTTP body filtering/forged choice, active/ready/completed, alt isolation and restored qualification.");
    }
    private static void Require(bool value, string message) { if (!value) throw new TestFailure(message); }
}
