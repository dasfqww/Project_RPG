using System.Collections.Immutable;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

internal static class DungeonObjectiveCases
{
    private const string Quest = "TEST.Dungeon";
    private const string Dungeon = "quest_dungeon", Difficulty = "Normal", Server = "quest-evidence-server";
    private sealed class Clock(DateTimeOffset initial) : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = initial;
        public override DateTimeOffset GetUtcNow() => Now;
        public void Advance() => Now = Now.AddSeconds(2);
    }

    private static NarrativeCatalog Catalog()
    {
        QuestDefinition quest = new(Quest, 1, [], ["talk", "clear", "again"],
            ObjectivePrerequisites: new Dictionary<string, ImmutableArray<string>>
                { ["clear"] = ["talk"], ["again"] = ["clear"] }.ToImmutableDictionary(),
            DungeonObjectives: new Dictionary<string, DungeonClearObjective>
                { ["clear"] = new(Dungeon, Difficulty), ["again"] = new(Dungeon, Difficulty) }.ToImmutableDictionary());
        return new(7, [quest], [new("TEST.Chapter", [Quest], ["TEST.Unlock"])],
        [
            new(Quest + ".Offer", "NPC", "Accept", Quest, null, [new("accept", "Accept", DialogueEffect.AcceptQuest)]),
            // Deliberately collides with the server command's quest/objective IDs to test fingerprint domains.
            new(Quest, "NPC", "Prepare", Quest, QuestStatus.Active, [new("clear", "Prepare", DialogueEffect.RecordObjective, "talk")]),
            new(Quest + ".Report", "NPC", "Report", Quest, QuestStatus.ReadyToReport, [new("report", "Report", DialogueEffect.ReportQuest)])
        ]);
    }

    public static async Task RunAsync(IGameRepository game, Func<INarrativeRepository> reopen, string label)
    {
        NarrativeCatalog catalog = Catalog();
        var definition = catalog.Quests[Quest];
        var jsonOptions = new JsonSerializerOptions(JsonSerializerDefaults.Web) { Converters = { new JsonStringEnumConverter() } };
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(new { schemaVersion = 1, catalogVersion = 7,
            quests = catalog.Quests.Values, chapters = catalog.Chapters.Values,
            dialogues = catalog.Dialogues.Values.Select(d => d with { RequiredCompletedQuests = [] }) }, jsonOptions);
        Check(NarrativeCatalogLoader.Parse(json).Quests[Quest].DungeonObjectives!.Count == 2, "External dungeon goal authoring failed.");
        void Bad(QuestDefinition bad, string code)
        {
            try { _ = new NarrativeCatalog(7, [bad], catalog.Chapters.Values, catalog.Dialogues.Values); }
            catch (ArgumentException error) when (error.Message == code) { return; }
            throw new TestFailure("Dungeon definition accepted: " + code);
        }
        Bad(definition with { DungeonObjectives = ImmutableDictionary<string, DungeonClearObjective>.Empty.Add("absent", new(Dungeon, Difficulty)) }, "invalid_dungeon_objective");
        Bad(definition with { DungeonObjectives = ImmutableDictionary<string, DungeonClearObjective>.Empty.Add("clear", null!) }, "invalid_dungeon_objective");
        Bad(definition with { DungeonObjectives = ImmutableDictionary<string, DungeonClearObjective>.Empty.Add("clear", new("", Difficulty)) }, "invalid_dungeon_objective");
        Bad(definition with { DungeonObjectives = ImmutableDictionary<string, DungeonClearObjective>.Empty.Add("clear", new(Dungeon, "Impossible")) }, "invalid_dungeon_objective");
        DungeonContentCatalog content = new(new DungeonContentOptions { Entries = [new() {
            DungeonId = Dungeon, MapPackage = "/Engine/Maps/Entry", Difficulties = [Difficulty] }] });
        DungeonObjectiveCoordinator.ValidateTargets(catalog, content);
        try
        {
            DungeonObjectiveCoordinator.ValidateTargets(catalog, new(new DungeonContentOptions { Entries = [new() {
                DungeonId = "another_dungeon", MapPackage = "/Engine/Maps/Entry", Difficulties = [Difficulty] }] }));
            throw new TestFailure("Missing dungeon target accepted at startup.");
        }
        catch (InvalidOperationException error) when (error.Message.StartsWith("Narrative: Dungeon objective", StringComparison.Ordinal)) { }
        var bypass = catalog.Dialogues.Values.Append(new DialogueDefinition("TEST.Bypass", "NPC", "Bypass", Quest,
            QuestStatus.Active, [new("clear", "Clear", DialogueEffect.RecordObjective, "clear")]));
        try { _ = new NarrativeCatalog(7, [definition], catalog.Chapters.Values, bypass); throw new TestFailure("Dialogue bypass authored."); }
        catch (ArgumentException error) when (error.Message == "invalid_dialogue_choice") { }

        Clock clock = new(DateTimeOffset.FromUnixTimeSeconds(DateTimeOffset.UtcNow.ToUnixTimeSeconds()));
        QuestManager quests = new();
        NarrativeCoordinator Flow() => new(reopen(), catalog, quests, new(), new(quests), clock, (INarrativeDungeonEvidenceRepository)game);
        DungeonObjectiveCoordinator Goals() => new(reopen(), (INarrativeDungeonEvidenceRepository)game,
            catalog, quests, new(), Flow(), clock);
        async Task<GameCharacter> Character(string owner)
        {
            await game.UpsertAccountAsync(new(owner, owner, false, false), clock.Now, default);
            return await game.CreateCharacterAsync(owner, "D" + Guid.NewGuid().ToString("N")[..12], clock.Now, default);
        }
        var character = await Character("76561198000000131");
        var alt = await Character(character.SteamId);
        var foreign = await Character("76561198000000132");
        var excluded = await Character("76561198000000133");
        string growth = JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, clock.Now, default));
        async Task Choice(GameCharacter who, Guid requestId, long revision, string node, string choice) =>
            _ = await Flow().ChooseAsync(who.CharacterId, who.SteamId, new(requestId, revision, 7, node, choice), default);
        async Task<Guid> Clear(string difficulty = Difficulty, bool excludeMember = false, bool failDelivery = false)
        {
            clock.Advance();
            var session = await game.CreateDungeonSessionAsync(character.SteamId, character.CharacterId, Dungeon, difficulty,
                clock.Now, clock.Now.AddHours(1), default);
            if (excludeMember)
                await game.JoinDungeonSessionAsync(session.DungeonSessionId, excluded.SteamId, excluded.CharacterId, 4,
                    clock.Now, clock.Now.AddHours(1), default);
            await game.AssignDungeonServerAsync(session.DungeonSessionId, Server, "127.0.0.1:7777", clock.Now, clock.Now.AddHours(1), default);
            await game.StartDungeonSessionAsync(session.DungeonSessionId, Server, clock.Now, clock.Now.AddHours(1), default);
            if (excludeMember)
            {
                var ticket = new AccessTokenService(clock).Issue(TimeSpan.FromMinutes(5));
                Check(await game.StoreJoinTicketAsync(ticket.TokenHash, excluded.SteamId, excluded.CharacterId,
                    session.DungeonSessionId, ticket.ExpiresAt, default) == JoinTicketIssueResult.Issued, "Excluded member ticket failed.");
                var admission = await game.ConsumeJoinTicketAsync(ticket.TokenHash, Server, session.DungeonSessionId, clock.Now, default);
                Check(admission is not null, "Excluded member admission failed.");
                await game.RecordDungeonMemberDisconnectionAsync(session.DungeonSessionId, Server, excluded.SteamId,
                    excluded.CharacterId, admission!.ConnectionGeneration, clock.Now, default);
                clock.Now = clock.Now.AddMinutes(10);
            }
            var settlement = await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId, Server,
                "quest-proof", new string('a', 64), [], [], clock.Now, default);
            Check(settlement.CharacterIds.Contains(character.CharacterId), "Clear recipient missing.");
            var evidence = await ((INarrativeDungeonEvidenceRepository)game).LoadNarrativeDungeonClearEvidenceAsync(session.DungeonSessionId, character.CharacterId, default);
            Check(evidence is not null && evidence.OccurredAt == clock.Now, "Pending settlement did not provide frozen evidence.");
            var claim = await game.ClaimNextDungeonRewardSettlementAsync("quest-test", clock.Now, clock.Now.AddMinutes(1), default);
            Check(claim?.DungeonSessionId == session.DungeonSessionId, "Unexpected private test settlement.");
            if (failDelivery) await game.FailDungeonRewardSettlementAsync(session.DungeonSessionId, "quest-test", "Fixture delivery failure", clock.Now, default);
            else await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId, "quest-test", clock.Now, default);
            return session.DungeonSessionId;
        }
        DungeonObjectiveCommand Command(Guid session, long revision, string objective = "clear", Guid? id = null) =>
            new(id ?? Guid.NewGuid(), revision, 7, session, Quest, objective);
        Task<NarrativeView> Record(DungeonObjectiveCommand command, GameCharacter? who = null) =>
            Goals().RecordAsync((who ?? character).CharacterId, (who ?? character).SteamId, command, default);

        Guid beforeAccept = await Clear();
        await Rejected(() => Record(Command(beforeAccept, 0)), "dungeon_objective_not_available");
        clock.Advance();
        await Choice(character, Guid.NewGuid(), 0, Quest + ".Offer", "accept");
        await Rejected(() => Record(Command(beforeAccept, 1)), "dungeon_objective_not_available");
        Guid beforePrerequisite = await Clear();
        clock.Advance();
        Guid dialogueReceipt = Guid.NewGuid();
        await Choice(character, dialogueReceipt, 1, Quest, "clear");
        await Rejected(() => Record(Command(beforeAccept, 2)), "dungeon_objective_event_out_of_window");
        await Rejected(() => Record(Command(beforePrerequisite, 2)), "dungeon_objective_event_out_of_window");
        Check((await Flow().LoadAsync(character.CharacterId, character.SteamId, default)).DungeonObjectiveCandidates.IsEmpty,
            "Discovery offered an event before objective activation.");
        var validSession = await Clear(excludeMember: true, failDelivery: true);
        var discovered = await Flow().LoadAsync(character.CharacterId, character.SteamId, default);
        Check(discovered.State.Revision == 2 && discovered.DungeonObjectiveCandidates.SequenceEqual([new DungeonObjectiveCandidate(Quest, "clear", validSession)]),
            "Read-only discovery lost eligible frozen evidence or wrote progress.");
        var boundary = new NarrativeDungeonObjectiveQuery(Quest, "clear", Dungeon, Difficulty, clock.Now.AddTicks(1), []);
        Check((await ((INarrativeDungeonEvidenceRepository)game).FindNarrativeDungeonObjectiveCandidatesAsync(
            character.CharacterId, character.SteamId, [boundary], default)).IsEmpty,
            "Discovery rounded activation backward across the persistent timestamp boundary.");
        await Rejected(() => Record(Command(validSession, 0), alt), "dungeon_clear_evidence_not_authorized");
        await Rejected(() => Goals().RecordAsync(character.CharacterId, foreign.SteamId, Command(validSession, 2), default), "dungeon_clear_evidence_not_authorized");
        Check(await ((INarrativeDungeonEvidenceRepository)game).LoadNarrativeDungeonClearEvidenceAsync(validSession, excluded.CharacterId, default) is null,
            "Expired disconnected member gained clear evidence.");
        await Rejected(() => Record(Command(Guid.NewGuid(), 2)), "dungeon_clear_evidence_not_authorized");
        await Rejected(() => Record(Command(validSession, 1, id: dialogueReceipt)), "idempotency_conflict");
        await Rejected(() => Record(Command(validSession, 2) with { CatalogVersion = 6 }), "narrative_catalog_changed");
        await Rejected(() => Record(Command(validSession, 1)), "narrative_revision_conflict");
        await Rejected(() => Record(Command(validSession, 2, "again")), "dungeon_objective_not_available");
        await Rejected(() => Record(Command(validSession, 2, "talk")), "dungeon_objective_not_available");
        Guid winningRequest = Guid.Empty;
        var command = Command(validSession, 2);
        var results = await Task.WhenAll(Attempt(command), Attempt(command with { RequestId = Guid.NewGuid() }));
        Check(results.Count(r => r is null) == 1 && results.Count(r => r == "narrative_revision_conflict") == 1, "Concurrent evidence double advanced.");
        // The winning request is not necessarily the first PostgreSQL task.
        if (results[0] is not null) command = command with { RequestId = winningRequest };
        Check((await Flow().LoadAsync(character.CharacterId, character.SteamId, default)).State.Revision == 3, "Clear revision mismatch.");
        Check((await Flow().LoadAsync(character.CharacterId, character.SteamId, default)).DungeonObjectiveCandidates.IsEmpty,
            "Discovery reused a clear for a sequential objective.");
        await Rejected(() => Record(Command(validSession, 3, "again")), "dungeon_objective_evidence_already_used");
        Guid wrongDifficulty = await Clear("Hard");
        await Rejected(() => Record(Command(wrongDifficulty, 3, "again")), "dungeon_objective_target_mismatch");
        Guid next = await Clear();
        var second = Command(next, 3, "again");
        var ready = await Record(second);
        Check(ready.State.Quests.Single().Status == QuestStatus.ReadyToReport && ready.State.StoryUnlocks.IsEmpty, "Clear bypassed report.");
        await Choice(character, Guid.NewGuid(), 4, Quest + ".Report", "report");
        var restored = await Flow().LoadAsync(character.CharacterId, character.SteamId, default);
        Check(restored.DungeonObjectiveCandidates.IsEmpty, "Completed quest still offered gameplay evidence.");
        Check(restored.State.Revision == 5 && restored.State.StoryUnlocks.SequenceEqual(["TEST.Unlock"]), "Restored report/unlock failed.");
        Check((await Record(command)).State.Revision == 3 && (await Record(second)).State.Revision == 4, "Stable evidence receipt replay failed.");
        await Rejected(() => Record(command with { DungeonSessionId = next }), "idempotency_conflict");
        // Cross-domain replay must also fail in the player-choice direction.
        await Rejected(() => Flow().ChooseAsync(character.CharacterId, character.SteamId,
            new(command.RequestId, 2, 7, Quest, "clear"), default), "idempotency_conflict");
        Check((await Flow().LoadAsync(alt.CharacterId, alt.SteamId, default)).State.Quests.IsEmpty, "Clear leaked to alt.");
        Check(growth == JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, clock.Now, default)), "Objective changed growth.");
        await Http(game, clock, catalog, character, alt, foreign, Flow(), Goals(), command);
        Console.WriteLine($"PASS Dungeon objectives {label}: external typed goals, dialogue bypass, frozen recipients, activation window, target, ordering, distinct clears, CAS/replay/domain isolation, restore/report, HTTP bearer roles and growth isolation.");

        async Task<string?> Attempt(DungeonObjectiveCommand attempt)
        {
            try { await Record(attempt); winningRequest = attempt.RequestId; return null; }
            catch (NarrativeConflictException error) { return error.Message; }
        }
    }

    private static async Task Http(IGameRepository game, Clock clock, NarrativeCatalog catalog, GameCharacter character,
        GameCharacter alt, GameCharacter foreign, NarrativeCoordinator flow, DungeonObjectiveCoordinator goals, DungeonObjectiveCommand receipt)
    {
        AccessTokenService tokens = new(clock);
        async Task<string> Token(string owner)
        {
            var issued = tokens.Issue(TimeSpan.FromMinutes(5));
            await game.StoreSessionAsync(issued.TokenHash, owner, issued.ExpiresAt, default);
            return issued.Token;
        }
        string ownToken = await Token(character.SteamId), foreignToken = await Token(foreign.SteamId);
        var serverToken = tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreGameServerCredentialAsync(serverToken.TokenHash, Server, receipt.DungeonSessionId, serverToken.ExpiresAt, default);
        string adminToken = Guid.NewGuid().ToString("N");
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IGameRepository>(game); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<TimeProvider>(clock); builder.Services.AddSingleton(flow); builder.Services.AddSingleton(goals);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = adminToken }));
        builder.Services.Configure<SecurityTelemetryOptions>(_ => { }); builder.Services.AddScoped<BearerAuthenticator>();
        await using var app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi(); await app.StartAsync();
        try
        {
            string address = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single();
            using HttpClient client = new() { BaseAddress = new Uri(address) };
            string route = $"/api/characters/{character.CharacterId}/narrative/dungeon-objectives";
            object Body(string revision) => new { receipt.RequestId, expectedRevision = revision, receipt.CatalogVersion,
                receipt.DungeonSessionId, receipt.QuestId, receipt.ObjectiveId };
            Check((await client.PostAsJsonAsync(route, Body("2"))).StatusCode == HttpStatusCode.Unauthorized, "Anonymous clear HTTP allowed.");
            foreach (string denied in new[] { foreignToken, adminToken, serverToken.Token })
            {
                client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", denied);
                Check((await client.PostAsJsonAsync(route, Body("2"))).StatusCode == HttpStatusCode.Forbidden, "Foreign/admin/post-session server clear HTTP allowed.");
            }
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", ownToken);
            var response = await client.PostAsJsonAsync(route, Body("2"));
            Check(response.IsSuccessStatusCode && response.Headers.CacheControl?.NoStore == true, "HTTP evidence replay failed.");
            using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            Check(json.RootElement.GetProperty("revision").GetString() == "3", "Evidence HTTP decimal revision mismatch.");
            Check(!json.RootElement.GetProperty("quests")[0].TryGetProperty("dungeonObjectives", out _), "Evidence internals leaked to journal.");
            Check((await client.PostAsJsonAsync(route, Body("2.5"))).StatusCode == HttpStatusCode.BadRequest, "Fractional evidence revision accepted.");
            Check((await client.PostAsJsonAsync($"/api/characters/{alt.CharacterId}/narrative/dungeon-objectives", Body("2"))).StatusCode == HttpStatusCode.Forbidden,
                "Alt used another character's clear.");
            response = await client.PostAsJsonAsync(route, new { requestId = Guid.NewGuid(), expectedRevision = "5", catalogVersion = catalog.Version,
                dungeonSessionId = Guid.NewGuid(), questId = Quest, objectiveId = "clear", cleared = true, count = int.MaxValue });
            Check(response.StatusCode == HttpStatusCode.Forbidden, "Forged client outcome became evidence.");
        }
        finally { await app.StopAsync(); }
    }

    private static async Task Rejected(Func<Task> action, string code)
    {
        try { await action(); }
        catch (NarrativeConflictException error) when (error.Message == code) { return; }
        throw new TestFailure("Expected dungeon objective rejection: " + code);
    }
    private static void Check(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
