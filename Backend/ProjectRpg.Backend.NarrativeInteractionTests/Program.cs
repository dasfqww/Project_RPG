using System.Collections.Immutable;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

try
{
    Check(args.Length == 0 || args.SequenceEqual(["--postgres"]), "Unknown arguments");
    InMemoryTransactionGate gate = new();
    InMemoryGameRepository memory = new(gate, new InMemoryItemRepository(gate));
    await Cases(memory, () => memory, "Memory");
    if (args.Length != 0)
    {
        Check(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_RUN"), "N", out _), "Owned runner required");
        NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_POSTGRES") ?? "");
        Check(config.Host == "127.0.0.1" && config.Database == "narrative_test", "Private DB required");
        await using var source = NpgsqlDataSource.Create(config.ConnectionString);
        var migrations = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
        await PostgresSchemaMigrations.ApplyAsync(source, migrations, default);
        await PostgresSchemaMigrations.ValidateAsync(source, migrations, default);
        PostgresGameRepository Repo() => new(source, NullLogger<PostgresGameRepository>.Instance);
        await Cases(Repo(), () => Repo(), "PostgreSQL reopened repositories");
    }
    Console.WriteLine("PASS personal interaction event groups"); return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine("FAIL interaction: " + (error is Failure or NarrativeConflictException ? error.Message
        : error is PostgresException pg ? "SQLSTATE " + pg.SqlState : error.GetType().Name));
    Console.Error.WriteLine(error.StackTrace); return 1;
}

static NarrativeCatalog Catalog()
{
    QuestDefinition Definition(string id) => new(id, 1, [], ["talk", "first", "second"],
        ObjectivePrerequisites: new Dictionary<string, ImmutableArray<string>> { ["first"] = ["talk"], ["second"] = ["first"] }.ToImmutableDictionary(),
        InteractionObjectives: new Dictionary<string, InteractionObjective> { ["first"] = new("test_dungeon", "Normal", "clue"), ["second"] = new("test_dungeon", "Normal", "clue") }.ToImmutableDictionary());
    var quests = new[] { Definition("TEST.A"), Definition("TEST.B") };
    return new(8, quests, [new("TEST.Chapter", ["TEST.A"], ["TEST.Unlock"])], quests.SelectMany(q => new DialogueDefinition[] {
        new(q.Id+".Offer", "NPC", "Accept", q.Id, null, [new("accept", "Accept", DialogueEffect.AcceptQuest)]),
        new(q.Id+".Talk", "NPC", "Prepare", q.Id, QuestStatus.Active, [new("talk", "Prepare", DialogueEffect.RecordObjective, "talk")]),
        new(q.Id+".Report", "NPC", "Report", q.Id, QuestStatus.ReadyToReport, [new("report", "Report", DialogueEffect.ReportQuest)]) }));
}

static async Task Cases(IGameRepository game, Func<INarrativeInteractionRepository> reopen, string label)
{
    var clock = new Clock(DateTimeOffset.FromUnixTimeSeconds(DateTimeOffset.UtcNow.ToUnixTimeSeconds()));
    var catalog = Catalog(); QuestManager quests = new();
    NarrativeCoordinator Narrative() => new((INarrativeRepository)game, catalog, quests, new(), new(quests), clock);
    NarrativeInteractionCoordinator Events() => new(reopen(), catalog, quests, new(), clock);
    var jsonOptions = new JsonSerializerOptions(JsonSerializerDefaults.Web) { Converters = { new JsonStringEnumConverter() } };
    byte[] definitionJson = JsonSerializer.SerializeToUtf8Bytes(new { schemaVersion = 1, catalogVersion = 8,
        quests = catalog.Quests.Values, chapters = catalog.Chapters.Values,
        dialogues = catalog.Dialogues.Values.Select(d => d with { RequiredCompletedQuests = [] }) }, jsonOptions);
    Check(NarrativeCatalogLoader.Parse(definitionJson).Quests["TEST.A"].InteractionObjectives!.Count == 2, "External interaction authoring failed");
    try
    {
        var bypass = catalog.Dialogues.Values.Append(new DialogueDefinition("TEST.Bypass", "NPC", "Invalid", "TEST.A", QuestStatus.Active,
            [new("confirm", "Confirm", DialogueEffect.RecordObjective, "first")]));
        _ = new NarrativeCatalog(8, catalog.Quests.Values, catalog.Chapters.Values, bypass);
        throw new Failure("Dialogue bypass allowed");
    }
    catch (ArgumentException error) when (error.Message == "invalid_dialogue_choice") { }
    try
    {
        var firstDefinition = catalog.Quests["TEST.A"];
        _ = new NarrativeCatalog(8, [firstDefinition with { DungeonObjectives = ImmutableDictionary<string, DungeonClearObjective>.Empty.Add("first", new("test_dungeon", "Normal")) }, catalog.Quests["TEST.B"]],
            catalog.Chapters.Values, catalog.Dialogues.Values);
        throw new Failure("Overlapping objective types accepted");
    }
    catch (ArgumentException error) when (error.Message == "invalid_interaction_objective") { }
    async Task<GameCharacter> Character(string owner)
    {
        await game.UpsertAccountAsync(new(owner, owner, false, false), clock.Now, default);
        return await game.CreateCharacterAsync(owner, "I"+Guid.NewGuid().ToString("N")[..12], clock.Now, default);
    }
    var character = await Character("76561198000000231"); var alt = await Character(character.SteamId);
    var foreign = await Character("76561198000000232");
    var session = await game.CreateDungeonSessionAsync(character.SteamId, character.CharacterId, "test_dungeon", "Normal", clock.Now, clock.Now.AddHours(2), default);
    await game.AssignDungeonServerAsync(session.DungeonSessionId, "interaction-test", "127.0.0.1:7777", clock.Now, clock.Now.AddHours(2), default);
    await game.StartDungeonSessionAsync(session.DungeonSessionId, "interaction-test", clock.Now, clock.Now.AddHours(2), default);
    async Task<long> Admit()
    {
        var ticket = new AccessTokenService(clock).Issue(TimeSpan.FromMinutes(10));
        Check(await game.StoreJoinTicketAsync(ticket.TokenHash, character.SteamId, character.CharacterId, session.DungeonSessionId, ticket.ExpiresAt, default) == JoinTicketIssueResult.Issued, "Ticket failed");
        var result = await game.ConsumeJoinTicketAsync(ticket.TokenHash, "interaction-test", session.DungeonSessionId, clock.Now, default);
        Check(result is not null, "Consume failed"); return result!.ConnectionGeneration;
    }
    long generation = await Admit();
    NarrativeInteractionCommand Event(string target = "clue", Guid? id = null) => new(id ?? Guid.NewGuid(), target, generation, clock.Now);
    Task<NarrativeInteractionResult> Record(NarrativeInteractionCommand command) => Events().RecordAsync(session.DungeonSessionId, character.CharacterId, "interaction-test", command, default);
    var beforeAccept = Event();
    await Reject(() => Record(beforeAccept), "interaction_not_authorized");
    Check(await game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId, "interaction-test", character.SteamId, character.CharacterId, generation, clock.Now, default), "Confirm failed");
    string growth = JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, clock.Now, default));
    var noEffect = await Record(beforeAccept);
    Check(noEffect.Revision == 0 && noEffect.AppliedObjectives.IsEmpty, "Unaccepted event progressed a quest");
    async Task Choice(long revision, string node, string choice, Guid? id = null) =>
        _ = await Narrative().ChooseAsync(character.CharacterId, character.SteamId, new(id ?? Guid.NewGuid(), revision, 8, node, choice), default);
    await Reject(() => Choice(0, "TEST.A.Offer", "accept", beforeAccept.EventId), "idempotency_conflict");
    await Choice(0, "TEST.A.Offer", "accept"); await Choice(1, "TEST.B.Offer", "accept");
    var beforePrerequisite = Event(); Check((await Record(beforePrerequisite)).AppliedObjectives.IsEmpty, "Prerequisite bypassed");
    await Choice(2, "TEST.A.Talk", "talk"); await Choice(3, "TEST.B.Talk", "talk");
    Check((await Record(beforeAccept)).Revision == 0 && (await Record(beforePrerequisite)).Revision == 2, "No-effect receipt credited retroactively");
    await Reject(() => Record(beforeAccept with { TargetId = "other" }), "idempotency_conflict");
    clock.Now = clock.Now.AddMinutes(6);
    var stale = Event() with { ObservedAtUtc = clock.Now.AddMinutes(-6) };
    await Reject(() => Record(stale), "interaction_event_out_of_window");
    var future = Event() with { ObservedAtUtc = clock.Now.AddMinutes(1) };
    await Reject(() => Record(future), "interaction_event_out_of_window");
    await Reject(() => Events().RecordAsync(session.DungeonSessionId, alt.CharacterId, "interaction-test", Event(), default), "interaction_not_authorized");
    await Reject(() => Events().RecordAsync(session.DungeonSessionId, character.CharacterId, "foreign-server", Event(), default), "interaction_not_authorized");
    clock.Now = clock.Now.AddSeconds(2); var first = Event(); clock.Now = clock.Now.AddSeconds(1);
    var concurrent = await Task.WhenAll(Record(first), Record(first));
    Check(concurrent.All(r => r.Revision == 5 && r.AppliedObjectives.Length == 2), "Concurrent replay duplicated a stage or missed another eligible quest");
    var late = await Record(Event() with { ObservedAtUtc = first.ObservedAtUtc });
    Check(late.Revision == 5 && late.AppliedObjectives.IsEmpty, "One old event cascaded into the next sequential stage");
    var second = Event(); var ready = await Record(second);
    Check(ready.Revision == 6 && ready.AppliedObjectives.Length == 2, "Next independent action did not complete both personal goals");
    await Choice(6, "TEST.A.Report", "report");
    Check(JsonSerializer.Serialize(await Record(first)) == JsonSerializer.Serialize(concurrent[0]), "Report or repository reopen changed the stored event result");
    Check((await Record(first with { ObservedAtUtc = first.ObservedAtUtc.ToOffset(TimeSpan.FromHours(9)) })).Revision == 5, "Equivalent timestamps changed fingerprint");
    await Reject(() => Choice(7, "TEST.B.Report", "report", first.EventId), "idempotency_conflict");
    Check((await Narrative().LoadAsync(character.CharacterId, character.SteamId, default)).State.StoryUnlocks.Contains("TEST.Unlock"), "Report did not project chapter");
    Check((await Narrative().LoadAsync(alt.CharacterId, alt.SteamId, default)).State.Revision == 0, "Progress leaked to alt");
    await Http(game, clock, Events(), character, foreign, session.DungeonSessionId, first);
    await game.RecordDungeonMemberDisconnectionAsync(session.DungeonSessionId, "interaction-test", character.SteamId, character.CharacterId, generation, clock.Now, default);
    await Reject(() => Record(Event()), "interaction_not_authorized"); Check((await Record(first)).Revision == 5, "Disconnect invalidated committed receipt");
    long previous = generation; generation = await Admit();
    await Reject(() => Record(Event()), "interaction_not_authorized");
    await game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId, "interaction-test", character.SteamId, character.CharacterId, generation, clock.Now, default);
    await Reject(() => Record(Event() with { ConnectionGeneration = previous }), "interaction_not_authorized");
    Check((await Record(Event("unused"))).AppliedObjectives.IsEmpty, "Unrelated interaction granted progress");
    if (game is PostgresGameRepository)
    {
        await using var connection = new NpgsqlConnection(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_POSTGRES"));
        await connection.OpenAsync();
        async Task SetOwner(string owner)
        {
            await using var change = new NpgsqlCommand("UPDATE dungeon_session_members SET steam_id=@owner WHERE character_id=@id AND dungeon_session_id=@session", connection);
            change.Parameters.AddWithValue("owner", owner); change.Parameters.AddWithValue("id", character.CharacterId); change.Parameters.AddWithValue("session", session.DungeonSessionId);
            await change.ExecuteNonQueryAsync();
        }
        try { await SetOwner(foreign.SteamId); await Reject(() => Record(Event()), "interaction_not_authorized"); }
        finally { await SetOwner(character.SteamId); }
    }
    var changedCatalog = new NarrativeCatalog(9, catalog.Quests.Values, catalog.Chapters.Values, catalog.Dialogues.Values);
    Check((await new NarrativeInteractionCoordinator(reopen(), changedCatalog, quests, new(), clock).RecordAsync(
        session.DungeonSessionId, character.CharacterId, "interaction-test", first, default)).Revision == 5, "Catalog update changed an existing receipt");
    Check(growth == JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, clock.Now, default)), "Interaction changed growth");
    await game.FinishDungeonSessionAsync(session.DungeonSessionId, "interaction-test", DungeonSessionState.Failed, clock.Now, default);
    await Reject(() => Record(Event()), "interaction_not_authorized");
    await Http(game, clock, Events(), character, foreign, session.DungeonSessionId, first, terminal: true);
    var sourceState = (await Narrative().LoadAsync(character.CharacterId, character.SteamId, default)).State;
    var badQuest = sourceState.Quests[0]; var entries = badQuest.InteractionObjectives!;
    badQuest = badQuest with { InteractionObjectives = entries.SetItem("second", entries["second"] with { EventId = entries["first"].EventId }) };
    var corrupt = sourceState with { CharacterId = alt.CharacterId, Revision = 1, Quests = sourceState.Quests.SetItem(0, badQuest) };
    await ((INarrativeRepository)game).CommitNarrativeAsync(alt.CharacterId, alt.SteamId, new(Guid.NewGuid(), 0, 8, "fixture", "corrupt"), _ => corrupt, clock.Now, default);
    await Reject(() => Narrative().LoadAsync(alt.CharacterId, alt.SteamId, default), "incompatible_interaction_objective_state");
    Console.WriteLine($"PASS {label}: authored goals, no-op receipts, order/window/generation/confirmation, atomic duplicate replay, multi-quest projection/report, bearer HTTP roles, alt/growth isolation");
}

static async Task Http(IGameRepository game, Clock clock, NarrativeInteractionCoordinator events, GameCharacter character,
    GameCharacter foreign, Guid session, NarrativeInteractionCommand command, bool terminal = false)
{
    AccessTokenService tokens = new(clock); var server = tokens.Issue(TimeSpan.FromMinutes(10)); var player = tokens.Issue(TimeSpan.FromMinutes(10));
    await game.StoreGameServerCredentialAsync(server.TokenHash, "interaction-test", session, server.ExpiresAt, default);
    await game.StoreSessionAsync(player.TokenHash, foreign.SteamId, player.ExpiresAt, default);
    string admin = Guid.NewGuid().ToString("N");
    var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
    builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
    builder.Services.AddSingleton<IGameRepository>(game); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<TimeProvider>(clock);
    builder.Services.AddSingleton(events); builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
    builder.Services.Configure<SecurityTelemetryOptions>(_ => { }); builder.Services.AddScoped<BearerAuthenticator>();
    // Existing read/choice handlers remain mapped with an explicit service boundary.
    builder.Services.AddSingleton(new NarrativeCoordinator((INarrativeRepository)game, Catalog(), new(), new(), new(new()), clock));
    await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi(); await app.StartAsync();
    try
    {
        using HttpClient client = new() { BaseAddress = new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
        string route = $"/api/dungeon-sessions/{session}/narrative/interactions/{character.CharacterId}";
        object body = new { command.EventId, command.TargetId, connectionGeneration = command.ConnectionGeneration.ToString(), command.ObservedAtUtc };
        Check((await client.PostAsJsonAsync(route, body)).StatusCode == HttpStatusCode.Unauthorized, "Anonymous event accepted");
        foreach (string token in new[] { player.Token, admin })
        {
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
            Check((await client.PostAsJsonAsync(route, body)).StatusCode == HttpStatusCode.Forbidden, "Player/admin attestation accepted");
        }
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", server.Token);
        var response = await client.PostAsJsonAsync(route, body);
        if (terminal)
        {
            Check(response.StatusCode == HttpStatusCode.Forbidden, "Restricted post-session credential wrote narrative progress");
            return;
        }
        Check(response.IsSuccessStatusCode && response.Headers.CacheControl?.NoStore == true, "Scoped server replay failed");
        using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
        Check(json.RootElement.GetProperty("revision").GetString() == "5" && json.RootElement.GetProperty("appliedObjectives").GetArrayLength() == 2
            && !json.RootElement.TryGetProperty("availableDialogues", out _), "Event response leaked journal or lost stable result");
        Check((await client.PostAsJsonAsync(route.Replace(session.ToString(), Guid.NewGuid().ToString()), body)).StatusCode == HttpStatusCode.Forbidden, "Server scope bypassed");
        response = await client.PostAsJsonAsync(route, new { eventId = Guid.NewGuid(), targetId = "clue", connectionGeneration = "1.5", command.ObservedAtUtc });
        Check(response.StatusCode == HttpStatusCode.BadRequest, "Fractional generation accepted");
    }
    finally { await app.StopAsync(); }
}
static async Task Reject(Func<Task> action, string code)
{
    try { await action(); } catch (NarrativeConflictException error) when (error.Message == code) { return; }
    throw new Failure("Expected rejection: " + code);
}
static void Check(bool condition, string message) { if (!condition) throw new Failure(message); }
sealed class Failure(string message) : Exception(message);
sealed class Clock(DateTimeOffset now) : TimeProvider { public DateTimeOffset Now { get; set; } = now; public override DateTimeOffset GetUtcNow() => Now; }
