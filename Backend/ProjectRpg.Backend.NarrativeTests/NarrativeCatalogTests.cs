using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Extensions.FileProviders;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Narrative;

internal static class NarrativeCatalogTests
{
    public static async Task RunAsync()
    {
        using Stream resource = typeof(NarrativeCatalog).Assembly.GetManifestResourceStream(
            "ProjectRpg.Backend.Narrative.Content.development.json")!;
        using StreamReader reader = new(resource);
        string source = (await reader.ReadToEndAsync()).Replace("DEV.", "AUTH.", StringComparison.Ordinal);
        NarrativeCatalog Parse(string text) => NarrativeCatalogLoader.Parse(Encoding.UTF8.GetBytes(text));
        string Changed(Action<JsonObject> edit)
        {
            JsonObject document = JsonNode.Parse(source)!.AsObject(); edit(document);
            return document.ToJsonString(new JsonSerializerOptions { Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
        }
        void Reject(string text, string code)
        {
            try { _ = Parse(text); }
            catch (NarrativeCatalogException error) when (error.Code == code) { return; }
            throw new TestFailure("Catalog should reject: " + code);
        }
        JsonObject Quest(JsonObject root) => root["quests"]![0]!.AsObject();
        JsonObject Node(JsonObject root, int index = 0) => root["dialogues"]![index]!.AsObject();
        NarrativeCatalog catalog = Parse(source);
        Require(catalog.Quests.ContainsKey("AUTH.Introduction") && catalog.Dialogues.Count == 4, "Authored IDs not loaded.");
        Require(NarrativeCatalog.Development.Quests.ContainsKey("DEV.Introduction"), "Embedded development content changed.");
        Reject(Changed(r => Quest(r)["title"] = " "), "invalid_quest_presentation");
        Reject(Changed(r => Quest(r)["title"] = new string('x', 257)), "invalid_quest_presentation");
        Reject(Changed(r => Quest(r)["objectiveTexts"]!["missing"] = "Unknown goal"), "invalid_quest_presentation");
        Reject(Changed(r => Quest(r)["objectiveTexts"]!["contact"] = new string('x', 1025)), "invalid_quest_presentation");
        var legacy = Parse(Changed(r => { Quest(r).Remove("title"); Quest(r).Remove("objectiveTexts"); }));
        Require(legacy.Quests.Values.Single().Title is null, "Legacy catalog gained an invented title.");
        _ = NarrativeCatalogLoader.Parse((byte[])[0xef, 0xbb, 0xbf, .. Encoding.UTF8.GetBytes(source)]);
        Reject(source.Replace("\"schemaVersion\": 1", "\"schemaVersion\": 1, \"schemaVersion\": 1"), "duplicate_narrative_json_property");
        Reject(Changed(r => r["schemaVersion"] = 2), "unsupported_narrative_catalog_schema");
        Reject(Changed(r => r["xpReward"] = 999), "invalid_narrative_catalog_json");
        Reject(Changed(r => r.Remove("catalogVersion")), "invalid_narrative_catalog_json");
        Reject(Changed(r => Quest(r)["prerequisites"] = JsonNode.Parse("[\"missing\"]")), "invalid_quest_definition");
        Reject(Changed(r => Quest(r)["prerequisites"] = JsonNode.Parse("[\"AUTH.Introduction\"]")), "cyclic_quest_prerequisites");
        Reject(Changed(r => r["quests"]!.AsArray().Add(Quest(r).DeepClone())), "invalid_narrative_catalog_definition");
        Reject(Changed(r => r["quests"]!.AsArray().Add((JsonNode?)null)), "invalid_narrative_catalog");
        Reject(Changed(r => Node(r)["choices"]![0]!["effect"] = 1), "invalid_narrative_catalog_json");
        Reject(Changed(r => Node(r)["choices"]![0]!["effect"] = "GrantXp"), "invalid_narrative_catalog_json");
        Reject(Changed(r => Node(r)["choices"]![0]!["xp"] = 900), "invalid_narrative_catalog_json");
        Reject(Changed(r => Node(r, 1)["choices"]![0]!["objectiveId"] = "missing"), "invalid_dialogue_choice");
        Reject(Changed(r => r["dialogues"]!.AsArray().RemoveAt(3)), "unreachable_quest_definition");
        Reject(Changed(r => Node(r)["text"] = new string('x', 4097)), "invalid_dialogue_definition");
        Reject(Changed(r => Quest(r)["objectives"] = JsonNode.Parse("[\"contact\",\"contact\"]")), "invalid_quest_definition");
        Reject(Changed(r => Node(r)["choices"]!.AsArray().Add(Node(r)["choices"]![0]!.DeepClone())), "invalid_dialogue_definition");
        Reject(Changed(r => r["chapters"]![0]!["requiredQuests"] = JsonNode.Parse("[\"missing\"]")), "invalid_chapter_definition");
        Reject(new string(' ', NarrativeCatalogLoader.MaximumBytes + 1), "narrative_catalog_too_large");
        Reject(Changed(r =>
        {
            var nodes = r["dialogues"]!.AsArray();
            for (int i = 4; i <= 256; i++) { var n = Node(r, 1).DeepClone(); n["id"] = "AUTH.Extra" + i; nodes.Add(n); }
        }), "narrative_catalog_limit_exceeded");
        Reject(Changed(r =>
        {
            var nodes = r["dialogues"]!.AsArray();
            for (int i = 4; i < 80; i++) { var n = Node(r, 1).DeepClone(); n["id"] = "AUTH.Extra" + i; n["text"] = new string('<', 4096); nodes.Add(n); }
        }), "narrative_catalog_response_too_large");

        var configuration = new ConfigurationBuilder().AddInMemoryCollection().Build();
        var environment = new TestEnvironment();
        Require(NarrativeCatalogLoader.LoadConfigured(configuration, environment).Quests.Count == 0, "Default catalog not empty.");
        configuration["Narrative:EnableDevelopmentContent"] = "true";
        Require(NarrativeCatalogLoader.LoadConfigured(configuration, environment).Dialogues.Count == 4, "Development opt-in failed.");
        environment.EnvironmentName = "Production";
        ExpectConfiguration("development_narrative_not_allowed");
        configuration["Narrative:CatalogPath"] = "content.json";
        ExpectConfiguration("ambiguous_narrative_catalog");
        configuration["Narrative:EnableDevelopmentContent"] = "false";
        configuration["Narrative:CatalogPath"] = "not-present-" + Guid.NewGuid().ToString("N");
        ExpectConfiguration("narrative_catalog_file_unavailable");
        string temporaryFile = Path.Combine(AppContext.BaseDirectory, "catalog-" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            configuration["Narrative:CatalogPath"] = Path.GetFileName(temporaryFile);
            await File.WriteAllTextAsync(temporaryFile, source.Replace("AUTH.", "DEV.", StringComparison.Ordinal));
            ExpectConfiguration("development_narrative_not_allowed");
            await File.WriteAllTextAsync(temporaryFile, source);
            Require(NarrativeCatalogLoader.LoadConfigured(configuration, environment).Quests.ContainsKey("AUTH.Introduction"),
                "Production authored file/relative content-root resolution failed.");
        }
        finally { File.Delete(temporaryFile); }
        void ExpectConfiguration(string code)
        {
            try { _ = NarrativeCatalogLoader.LoadConfigured(configuration, environment); }
            catch (NarrativeCatalogException error) when (error.Code == code) { return; }
            throw new TestFailure("Configuration should reject: " + code);
        }

        InMemoryTransactionGate gate = new();
        var game = new InMemoryGameRepository(gate, new InMemoryItemRepository(gate));
        const string owner = "76561198000000001";
        await game.UpsertAccountAsync(new(owner, owner, false, false), DateTimeOffset.UtcNow, default);
        var character = await game.CreateCharacterAsync(owner, "AuthoringTest", DateTimeOffset.UtcNow, default);
        var flow = new NarrativeCoordinator(game, catalog, new QuestManager(), new ScenarioManager(),
            new DialogueManager(new QuestManager()), TimeProvider.System);
        string[] nodesToUse = ["Contact.Offer", "Contact.Explain", "Instructor.Explain", "Contact.Report"];
        string[] choices = ["accept", "confirm", "confirm", "report"];
        for (int i = 0; i < 4; i++)
            await flow.ChooseAsync(character.CharacterId, owner,
                new(Guid.NewGuid(), i, catalog.Version, "AUTH." + nodesToUse[i], choices[i]), default);
        var done = await flow.LoadAsync(character.CharacterId, owner, default);
        Require(done.State.CompletedChapters.Contains("AUTH.Prologue") && done.State.Revision == 4, "Authored flow did not complete.");
        Require(done.Journal.Single().Title == "출발 전 확인" && done.Journal.Single().Objectives.All(o => o.Completed),
            "Completed journal presentation is not derived from stored progress.");
        Console.WriteLine("PASS Narrative authoring: strict JSON, limits, references/cycles/reachability, configuration, renamed authored flow.");
    }
    private static void Require(bool value, string message) { if (!value) throw new TestFailure(message); }
    private sealed class TestEnvironment : IHostEnvironment
    {
        public string EnvironmentName { get; set; } = "Development";
        public string ApplicationName { get; set; } = "NarrativeCatalogTests";
        public string ContentRootPath { get; set; } = AppContext.BaseDirectory;
        public IFileProvider ContentRootFileProvider { get; set; } = new NullFileProvider();
    }
}
