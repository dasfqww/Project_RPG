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

internal static class NarrativeInformationTests
{
    public static async Task RunAsync()
    {
        using var stream = typeof(NarrativeInformationTests).Assembly.GetManifestResourceStream("NarrativeInformationFixture")!;
        using var reader = new StreamReader(stream);
        string source = await reader.ReadToEndAsync();
        var information = NarrativeCatalogLoader.Parse(Encoding.UTF8.GetBytes(source));
        Require(information.Dialogues.Count == 3 && information.Quests.Count == 0, "Information-only catalog failed.");
        void Reject(Action<JsonObject> edit, string code)
        {
            var json = JsonNode.Parse(source)!.AsObject(); edit(json);
            try { _ = NarrativeCatalogLoader.Parse(Encoding.UTF8.GetBytes(json.ToJsonString())); }
            catch (NarrativeCatalogException error) when (error.Code == code) { return; }
            throw new TestFailure("Information definition should reject: " + code);
        }
        Reject(j => j["dialogues"]![0]!["choices"]![0]!["nextDialogueId"] = "missing", "invalid_information_choice");
        Reject(j => j["dialogues"]![0]!["choices"]![0]!["effect"] = "AcceptQuest", "invalid_information_choice");
        Reject(j => j["dialogues"]![0]!["choices"]![0]!["objectiveId"] = "contact", "invalid_information_choice");
        Reject(j => j["dialogues"]![0]!["questId"] = "DEV.Introduction", "invalid_dialogue_definition");
        Reject(j => j["dialogues"]![0]!["requiredStatus"] = "Active", "invalid_dialogue_definition");
        Reject(j => j["dialogues"]![0]!["entryPoint"] = false, "unreachable_information_dialogue");
        var dev = NarrativeCatalog.Development;
        var catalog = new NarrativeCatalog(1, dev.Quests.Values, dev.Chapters.Values,
            dev.Dialogues.Values.Concat(information.Dialogues.Values));
        try
        {
            _ = new NarrativeCatalog(1, dev.Quests.Values, dev.Chapters.Values,
                catalog.Dialogues.Values.Select(d => d.Id == "INFO.Start" ? d with
                { Choices = [new("bad", "Bad link", DialogueEffect.None, NextDialogueId: "DEV.Contact.Offer")] } : d));
            throw new TestFailure("Information link targeted a progression node.");
        }
        catch (ArgumentException) { }

        var gate = new InMemoryTransactionGate();
        var game = new InMemoryGameRepository(gate, new InMemoryItemRepository(gate));
        const string owner = "76561198000000001";
        await game.UpsertAccountAsync(new(owner, owner, false, false), DateTimeOffset.UtcNow, default);
        var character = await game.CreateCharacterAsync(owner, "InformationTest", DateTimeOffset.UtcNow, default);
        var flow = new NarrativeCoordinator(game, catalog, new QuestManager(), new ScenarioManager(),
            new DialogueManager(new QuestManager()), TimeProvider.System);
        Guid request = Guid.NewGuid();
        try
        {
            await flow.ChooseAsync(character.CharacterId, owner, new(request, 0, 1, "INFO.Start", "ask"), default);
            throw new TestFailure("Information choice mutated server state.");
        }
        catch (NarrativeConflictException error) when (error.Message == "informational_dialogue_not_mutable") { }
        var before = await flow.LoadAsync(character.CharacterId, owner, default);
        Require(before.State.Revision == 0 && before.State.Quests.IsEmpty, "Information rejection changed state.");
        // Same request ID remains unused: the rejected information command created no receipt.
        await flow.ChooseAsync(character.CharacterId, owner, new(request, 0, 1, "DEV.Contact.Offer", "accept"), default);

        var tokens = new AccessTokenService(TimeProvider.System);
        var token = tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreSessionAsync(token.TokenHash, owner, token.ExpiresAt, default);
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IGameRepository>(game); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<TimeProvider>(TimeProvider.System); builder.Services.AddScoped<BearerAuthenticator>();
        builder.Services.Configure<AuthOptions>(_ => { }); builder.Services.Configure<SecurityTelemetryOptions>(_ => { });
        builder.Services.AddSingleton(flow);
        await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi();
        await app.StartAsync();
        try
        {
            using var client = new HttpClient { BaseAddress = new Uri(app.Services.GetRequiredService<IServer>()
                .Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token.Token);
            string route = $"/api/characters/{character.CharacterId}/narrative";
            using var json = JsonDocument.Parse(await client.GetStringAsync(route));
            var node = json.RootElement.GetProperty("availableDialogues").EnumerateArray().Single(n => n.GetProperty("id").GetString() == "INFO.Start");
            Require(node.GetProperty("informational").GetBoolean() && node.GetProperty("entryPoint").GetBoolean()
                && node.GetProperty("choices")[0].GetProperty("nextDialogueId").GetString() == "INFO.Topic", "Information wire metadata mismatch.");
            var result = await client.PostAsJsonAsync(route + "/choices", new { requestId = Guid.NewGuid(),
                expectedRevision = "1", catalogVersion = 1, dialogueId = "INFO.Detail", choiceId = "close" });
            Require(result.StatusCode == HttpStatusCode.Conflict, "Information POST not rejected over HTTP.");
            Require((await flow.LoadAsync(character.CharacterId, owner, default)).State.Revision == 1, "HTTP information POST advanced revision.");
        }
        finally { await app.StopAsync(); }
        Console.WriteLine("PASS Narrative information: graph/back links, invalid effects/targets/orphans, read metadata, forged POST rejection, no state/receipt mutation.");
    }
    private static void Require(bool value, string message) { if (!value) throw new TestFailure(message); }
}
