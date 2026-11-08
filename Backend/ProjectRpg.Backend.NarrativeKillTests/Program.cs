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
    InMemoryTransactionGate gate = new(); InMemoryGameRepository memory = new(gate, new InMemoryItemRepository(gate));
    await Cases(memory, () => memory, "Memory");
    if (args.Length != 0)
    {
        Check(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_RUN"), "N", out _), "Owned runner required");
        NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_POSTGRES") ?? "");
        Check(config.Host == "127.0.0.1" && config.Database == "narrative_test", "Private DB required");
        await using var source = NpgsqlDataSource.Create(config.ConnectionString);
        var migrations = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
        await PostgresSchemaMigrations.ApplyAsync(source, migrations, default); await PostgresSchemaMigrations.ValidateAsync(source, migrations, default);
        PostgresGameRepository Repo() => new(source, NullLogger<PostgresGameRepository>.Instance);
        await Cases(Repo(), () => Repo(), "Postgres reopened repositories");
    }
    Console.WriteLine("PASS personal kill objective groups"); return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine("FAIL kill: " + (error is Failure or NarrativeConflictException ? error.Message
        : error is PostgresException pg ? "SQLSTATE " + pg.SqlState : error.GetType().Name));
    Console.Error.WriteLine(error.StackTrace); return 1;
}

static NarrativeCatalog Catalog(int count = 2, int version = 8, string dungeon = "test_dungeon", string difficulty = "Normal", string enemy = "Enemy.Wolf")
{
    QuestDefinition Q(string id) => new(id, 1, [], ["talk", "first", "second"],
        ObjectivePrerequisites: new Dictionary<string, ImmutableArray<string>> { ["first"] = ["talk"], ["second"] = ["first"] }.ToImmutableDictionary(),
        KillObjectives: new Dictionary<string, KillObjective> { ["first"] = new(dungeon, difficulty, enemy, count),
            ["second"] = new(dungeon, difficulty, enemy, 1) }.ToImmutableDictionary());
    return new(version, [Q("TEST.A"), Q("TEST.B")], [new("TEST.Chapter", ["TEST.A"], ["TEST.Unlock"])],
        new[] { "TEST.A", "TEST.B" }.SelectMany(id => new DialogueDefinition[] {
            new(id+".Offer", "NPC", "Offer", id, null, [new("accept", "Accept", DialogueEffect.AcceptQuest)]),
            new(id+".Talk", "NPC", "Talk", id, QuestStatus.Active, [new("talk", "Talk", DialogueEffect.RecordObjective, "talk")]),
            new(id+".Report", "NPC", "Report", id, QuestStatus.ReadyToReport, [new("report", "Report", DialogueEffect.ReportQuest)]) }));
}
static async Task Cases(IGameRepository game, Func<INarrativeInteractionRepository> reopen, string label)
{
    var clock = new Clock(DateTimeOffset.FromUnixTimeSeconds(DateTimeOffset.UtcNow.ToUnixTimeSeconds()));
    var catalog = Catalog(); QuestManager quests = new();
    NarrativeCoordinator Narrative(NarrativeCatalog? alternate = null) => new((INarrativeRepository)game, alternate ?? catalog, quests, new(), new(quests), clock);
    NarrativeKillCoordinator Events(NarrativeCatalog? alternate = null) => new(reopen(), alternate ?? catalog, quests, new(), clock);
    var json = new JsonSerializerOptions(JsonSerializerDefaults.Web) { Converters = { new JsonStringEnumConverter() } };
    byte[] definition = JsonSerializer.SerializeToUtf8Bytes(new { schemaVersion = 1, catalogVersion = 8,
        quests = catalog.Quests.Values, chapters = catalog.Chapters.Values, dialogues = catalog.Dialogues.Values.Select(d => d with { RequiredCompletedQuests = [] }) }, json);
    Check(NarrativeCatalogLoader.Parse(definition).Quests["TEST.A"].KillObjectives!["first"].RequiredCount == 2, "JSON kill authoring failed");
    foreach (int invalid in new[] { 0, -1, 10001 }) BadDefinition(() => Catalog(invalid), "invalid_kill_objective");
    BadDefinition(() => Catalog(enemy: "bad enemy"), "invalid_kill_objective");
    BadDefinition(() => new NarrativeCatalog(8, catalog.Quests.Values, catalog.Chapters.Values, catalog.Dialogues.Values.Append(
        new DialogueDefinition("Bypass", "NPC", "Invalid", "TEST.A", QuestStatus.Active, [new("hit", "Hit", DialogueEffect.RecordObjective, "first")]))), "invalid_dialogue_choice");
    var overlap = catalog.Quests["TEST.A"] with { InteractionObjectives = ImmutableDictionary<string, InteractionObjective>.Empty.Add("first", new("test_dungeon", "Normal", "Enemy.Wolf")) };
    BadDefinition(() => new NarrativeCatalog(8, [overlap, catalog.Quests["TEST.B"]], catalog.Chapters.Values, catalog.Dialogues.Values), "invalid_kill_objective");
    async Task<GameCharacter> Character(string owner)
    {
        await game.UpsertAccountAsync(new(owner, owner, false, false), clock.Now, default);
        return await game.CreateCharacterAsync(owner, "K"+Guid.NewGuid().ToString("N")[..12], clock.Now, default);
    }
    var character = await Character("76561198000000331"); var alt = await Character(character.SteamId); var foreign = await Character("76561198000000332");
    var session = await game.CreateDungeonSessionAsync(character.SteamId, character.CharacterId, "test_dungeon", "Normal", clock.Now, clock.Now.AddHours(2), default);
    const string server = "kill-test";
    await game.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:7777", clock.Now, clock.Now.AddHours(2), default);
    await game.StartDungeonSessionAsync(session.DungeonSessionId, server, clock.Now, clock.Now.AddHours(2), default);
    async Task<long> Admit()
    {
        var ticket = new AccessTokenService(clock).Issue(TimeSpan.FromMinutes(10));
        Check(await game.StoreJoinTicketAsync(ticket.TokenHash, character.SteamId, character.CharacterId, session.DungeonSessionId, ticket.ExpiresAt, default) == JoinTicketIssueResult.Issued, "Ticket failed");
        var consumed = await game.ConsumeJoinTicketAsync(ticket.TokenHash, server, session.DungeonSessionId, clock.Now, default);
        Check(consumed is not null, "Consume failed"); return consumed!.ConnectionGeneration;
    }
    long generation = await Admit();
    NarrativeKillCommand Event(string enemy = "Enemy.Wolf") => new(Guid.NewGuid(), enemy, generation, clock.Now);
    Task<NarrativeKillResult> Record(NarrativeKillCommand command) => Events().RecordAsync(session.DungeonSessionId, character.CharacterId, server, command, default);
    var before = Event(); await Reject(() => Record(before), "kill_not_authorized");
    Check(await game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId, server, character.SteamId, character.CharacterId, generation, clock.Now, default), "Confirm failed");
    string growth = JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, clock.Now, default));
    Check((await Record(before)).Revision == 0, "Unaccepted event advanced a quest");
    async Task Choice(long revision, string node, string choice, Guid? id = null) =>
        _ = await Narrative().ChooseAsync(character.CharacterId, character.SteamId, new(id ?? Guid.NewGuid(), revision, 8, node, choice), default);
    await Reject(() => Choice(0, "TEST.A.Offer", "accept", before.EventId), "idempotency_conflict");
    await Choice(0, "TEST.A.Offer", "accept"); await Choice(1, "TEST.B.Offer", "accept");
    var early = Event(); Check((await Record(early)).AppliedObjectives.IsEmpty, "Prerequisite bypassed");
    await Choice(2, "TEST.A.Talk", "talk"); await Choice(3, "TEST.B.Talk", "talk");
    Check((await Record(Event("Enemy.Other"))).Revision == 4, "Wrong enemy counted while goals active");
    foreach (var mismatch in new[] { Catalog(dungeon: "other_dungeon"), Catalog(difficulty: "Hard") })
        Check((await Events(mismatch).RecordAsync(session.DungeonSessionId, character.CharacterId, server, Event(), default)).AppliedObjectives.IsEmpty, "Wrong dungeon/difficulty counted");
    var interactionCollision = Event();
    await new NarrativeInteractionCoordinator(reopen(), catalog, quests, new(), clock).RecordAsync(session.DungeonSessionId, character.CharacterId,
        server, new(interactionCollision.EventId, interactionCollision.EnemyId, generation, interactionCollision.ObservedAtUtc), default);
    await Reject(() => Record(interactionCollision), "idempotency_conflict");
    Check((await Record(before)).Revision == 0 && (await Record(early)).Revision == 2, "No-effect event counted retroactively");
    await Reject(() => Record(before with { EnemyId = "Other" }), "idempotency_conflict");
    await Reject(() => new NarrativeInteractionCoordinator(reopen(), catalog, quests, new(), clock).RecordAsync(
        session.DungeonSessionId, character.CharacterId, server, new(before.EventId, before.EnemyId, generation, before.ObservedAtUtc), default), "idempotency_conflict");
    clock.Now = clock.Now.AddMinutes(6);
    await Reject(() => Record(Event() with { ObservedAtUtc = clock.Now.AddMinutes(-6) }), "kill_event_out_of_window");
    await Reject(() => Record(Event() with { ObservedAtUtc = clock.Now.AddSeconds(1) }), "kill_event_out_of_window");
    await Reject(() => Record(Event() with { EventId = Guid.Empty }), "invalid_kill_event");
    await Reject(() => Record(Event() with { ConnectionGeneration = 0 }), "invalid_kill_event");
    var first = Event(); await Http(game, clock, Events(), character, foreign, session.DungeonSessionId, first);
    var concurrent = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Record(first)));
    Check(concurrent.All(r => r.Revision == 5 && r.AppliedObjectives.Length == 2 && r.AppliedObjectives.All(p => p.Count == 1 && p.RequiredCount == 2)), "Concurrent retries or supplied count advanced twice");
    var view = await Narrative().LoadAsync(character.CharacterId, character.SteamId, default);
    Check(view.Journal.All(q => q.Objectives.Single(o => o.Id == "first").CurrentCount == 1), "Partial count not projected to journal");
    var second = Event(); var counted = await Record(second);
    Check(counted.Revision == 6 && counted.AppliedObjectives.All(p => p.Count == 2), "Second death did not advance both parallel goals");
    Check((await Narrative().LoadAsync(character.CharacterId, character.SteamId, default)).State.Quests.All(q => !q.CompletedObjectives.Contains("second") && q.KillObjectives!["second"].Count == 0), "Single kill cascaded into next stage");
    var third = Event(); Check((await Record(third)).Revision == 7, "Next-stage kill failed");
    Check((await Narrative().LoadAsync(character.CharacterId, character.SteamId, default)).State.StoryUnlocks.IsEmpty, "Kills automatically reported/unlocked story");
    await Choice(7, "TEST.A.Report", "report"); await Choice(8, "TEST.B.Report", "report");
    Check(JsonSerializer.Serialize(await Record(first)) == JsonSerializer.Serialize(concurrent[0]), "Historical partial receipt changed after more kills/report");
    Check((await Record(first with { ObservedAtUtc = first.ObservedAtUtc.ToOffset(TimeSpan.FromHours(9)) })).Revision == 5, "Equivalent timestamp changed fingerprint");
    Check((await Events(Catalog(3, 9)).RecordAsync(session.DungeonSessionId, character.CharacterId, server, first, default)).AppliedObjectives.All(p => p.RequiredCount == 2), "Definition edit rewrote stored receipt");
    await Reject(() => Narrative(Catalog(3, 9)).LoadAsync(character.CharacterId, character.SteamId, default), "incompatible_kill_objective_state");
    Check((await Narrative().LoadAsync(alt.CharacterId, alt.SteamId, default)).State.Revision == 0, "Alt received progress");
    await game.RecordDungeonMemberDisconnectionAsync(session.DungeonSessionId, server, character.SteamId, character.CharacterId, generation, clock.Now, default);
    await Reject(() => Record(Event()), "kill_not_authorized"); Check((await Record(first)).Revision == 5, "Disconnect destroyed committed receipt");
    long previous = generation; generation = await Admit(); await Reject(() => Record(Event()), "kill_not_authorized");
    await game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId, server, character.SteamId, character.CharacterId, generation, clock.Now, default);
    await Reject(() => Record(Event() with { ConnectionGeneration = previous }), "kill_not_authorized");
    Check((await Record(Event("Other"))).AppliedObjectives.IsEmpty && (await Record(Event())).Revision == 9, "Completed goal exceeded required count");
    Check(growth == JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, clock.Now, default)), "Kill objectives changed growth");
    await game.FinishDungeonSessionAsync(session.DungeonSessionId, server, DungeonSessionState.Failed, clock.Now, default);
    await Reject(() => Record(Event()), "kill_not_authorized"); await Http(game, clock, Events(), character, foreign, session.DungeonSessionId, first, terminal: true);
    var sourceState = (await Narrative().LoadAsync(character.CharacterId, character.SteamId, default)).State;
    var badQuest = sourceState.Quests[0]; var entries = badQuest.KillObjectives!;
    badQuest = badQuest with { KillObjectives = entries.SetItem("second", entries["second"] with { LastEventId = entries["first"].LastEventId }) };
    var corrupt = sourceState with { CharacterId = alt.CharacterId, Revision = 1, Quests = sourceState.Quests.SetItem(0, badQuest) };
    await ((INarrativeRepository)game).CommitNarrativeAsync(alt.CharacterId, alt.SteamId, new(Guid.NewGuid(), 0, 8, "fixture", "corrupt"), _ => corrupt, clock.Now, default);
    await Reject(() => Narrative().LoadAsync(alt.CharacterId, alt.SteamId, default), "incompatible_kill_objective_state");
    var overflow = sourceState.Quests[0]; overflow = overflow with { KillObjectives = overflow.KillObjectives!.SetItem("first", overflow.KillObjectives["first"] with { Count = 3 }) };
    await ((INarrativeRepository)game).CommitNarrativeAsync(foreign.CharacterId, foreign.SteamId, new(Guid.NewGuid(), 0, 8, "fixture", "overflow"),
        _ => sourceState with { CharacterId = foreign.CharacterId, Revision = 1, Quests = sourceState.Quests.SetItem(0, overflow) }, clock.Now, default);
    await Reject(() => Narrative().LoadAsync(foreign.CharacterId, foreign.SteamId, default), "incompatible_kill_objective_state");
    Console.WriteLine("PASS "+label+": authoring, partial counters, concurrency, replay, ordering, role HTTP, generation, terminal, alt/growth and corrupt state");
}

static async Task Http(IGameRepository game, Clock clock, NarrativeKillCoordinator events, GameCharacter character,
    GameCharacter foreign, Guid session, NarrativeKillCommand command, bool terminal = false)
{
    AccessTokenService tokens = new(clock); var server = tokens.Issue(TimeSpan.FromMinutes(10)); var player = tokens.Issue(TimeSpan.FromMinutes(10));
    await game.StoreGameServerCredentialAsync(server.TokenHash, "kill-test", session, server.ExpiresAt, default);
    await game.StoreSessionAsync(player.TokenHash, foreign.SteamId, player.ExpiresAt, default);
    string admin = Guid.NewGuid().ToString("N");
    var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
    builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
    builder.Services.AddSingleton<IGameRepository>(game); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<TimeProvider>(clock);
    builder.Services.AddSingleton(events); builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
    builder.Services.Configure<SecurityTelemetryOptions>(_ => { }); builder.Services.AddScoped<BearerAuthenticator>();
    builder.Services.AddSingleton(new NarrativeCoordinator((INarrativeRepository)game, Catalog(), new(), new(), new(new()), clock));
    await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi(); await app.StartAsync();
    try
    {
        using HttpClient client = new() { BaseAddress = new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
        string route = $"/api/dungeon-sessions/{session}/narrative/kills/{character.CharacterId}";
        object body = new { command.EventId, command.EnemyId, connectionGeneration = command.ConnectionGeneration.ToString(), command.ObservedAtUtc, count = 999999 };
        Check((await client.PostAsJsonAsync(route, body)).StatusCode == HttpStatusCode.Unauthorized, "Anonymous kill accepted");
        foreach (string token in new[] { player.Token, admin })
        {
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
            Check((await client.PostAsJsonAsync(route, body)).StatusCode == HttpStatusCode.Forbidden, "Player/admin kill accepted");
        }
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", server.Token);
        var response = await client.PostAsJsonAsync(route, body);
        Check(response.StatusCode == (terminal ? HttpStatusCode.Forbidden : HttpStatusCode.OK), "Server kill role/terminal gate failed");
        if (terminal) return;
        Check(response.Headers.CacheControl?.NoStore == true, "Kill receipt cacheable");
        var value = await response.Content.ReadFromJsonAsync<JsonElement>();
        Check(value.GetProperty("revision").GetString() == "5" && value.GetProperty("appliedObjectives").EnumerateArray().All(p => p.GetProperty("count").GetInt32() == 1), "HTTP count or revision precision contract failed");
        Check((await client.PostAsJsonAsync(route.Replace(session.ToString(), Guid.NewGuid().ToString()), body)).StatusCode == HttpStatusCode.Forbidden, "Other session accepted");
        Check((await client.PostAsJsonAsync(route.Replace(character.CharacterId.ToString(), foreign.CharacterId.ToString()), body)).StatusCode == HttpStatusCode.Forbidden, "Nonmember accepted");
        Check((await client.PostAsJsonAsync(route, new { command.EventId, command.EnemyId, connectionGeneration = " 1", command.ObservedAtUtc })).StatusCode == HttpStatusCode.BadRequest, "Malformed generation accepted");
    }
    finally { await app.StopAsync(); }
}
static async Task Reject(Func<Task> action, string code)
{
    try { await action(); throw new Failure("Expected rejection: "+code); }
    catch (NarrativeConflictException error) when (error.Message == code) { }
}
static void BadDefinition(Action action, string code)
{
    try { action(); throw new Failure("Bad definition accepted"); }
    catch (ArgumentException error) when (error.Message == code) { }
}
static void Check(bool condition, string message) { if (!condition) throw new Failure(message); }
sealed class Failure(string message) : Exception(message);
sealed class Clock(DateTimeOffset now) : TimeProvider { public DateTimeOffset Now { get; set; } = now; public override DateTimeOffset GetUtcNow() => Now; }
