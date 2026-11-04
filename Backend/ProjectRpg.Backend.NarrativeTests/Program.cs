using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

try
{
    Require(args.Length == 0 || args.SequenceEqual(["--postgres"]), "Unknown arguments.");
    DefinitionCases();
    await NarrativeCatalogTests.RunAsync();
    InMemoryTransactionGate gate = new();
    InMemoryGameRepository memory = new(gate, new InMemoryItemRepository(gate));
    await RepositoryCases(memory, () => memory, "Memory");
    await HttpCases();
    if (args.Length != 0) await PostgresCases();
    Console.WriteLine("PASS Narrative: all requested groups completed.");
    return 0;
}
catch (Exception error)
{
    // Never log credentials, SQL or child environments.
    Console.Error.WriteLine("FAIL Narrative: " + (error is TestFailure ? error.Message
        : error is NarrativeConflictException ? error.Message
        : error is PostgresException pg ? "SQLSTATE " + pg.SqlState : error.GetType().Name));
    return 1;
}

static NarrativeCoordinator Coordinator(INarrativeRepository repository, NarrativeCatalog? catalog = null)
{
    QuestManager quests = new();
    return new(repository, catalog ?? NarrativeCatalog.Development, quests, new ScenarioManager(),
        new DialogueManager(quests), TimeProvider.System);
}
static NarrativeCommand Command(long revision, string dialogue, string choice) =>
    new(Guid.NewGuid(), revision, 1, "DEV." + dialogue, choice);
static async Task<GameCharacter> Character(IGameRepository game, string owner)
{
    await game.UpsertAccountAsync(new(owner, owner, false, false), DateTimeOffset.UtcNow, default);
    return await game.CreateCharacterAsync(owner, "N" + Guid.NewGuid().ToString("N")[..12], DateTimeOffset.UtcNow, default);
}
static string Json(object value) => JsonSerializer.Serialize(value);
static void DefinitionCases()
{
    try
    {
        _ = new NarrativeCatalog(1, [new("q", 1, ["q"], ["talk"])], [], []);
        throw new TestFailure("Cyclic prerequisites accepted.");
    }
    catch (ArgumentException) { }
    try
    {
        _ = new NarrativeCatalog(1, [new("q", 1, [], ["talk"])], [],
            [new("node", "npc", "line", "q", QuestStatus.Active,
                [new("choice", "confirm", DialogueEffect.RecordObjective, "forged")])]);
        throw new TestFailure("Unknown authored objective accepted.");
    }
    catch (ArgumentException) { }
    Console.WriteLine("PASS Narrative: invalid definitions and prerequisite cycles rejected.");
}
static async Task RepositoryCases(IGameRepository game, Func<INarrativeRepository> reopen, string label)
{
    const string owner = "76561198000000001", foreign = "76561198000000002";
    GameCharacter character = await Character(game, owner), alt = await Character(game, owner);
    NarrativeCoordinator flow = Coordinator(reopen());
    string growthBefore = Json((await ((ICharacterProgressionRepository)game)
        .LoadProgressionAsync(character.CharacterId, DateTimeOffset.UtcNow, default))!);
    await Reject(() => flow.LoadAsync(character.CharacterId, foreign, default), "character_not_owned");
    NarrativeView initial = await flow.LoadAsync(character.CharacterId, owner, default);
    Require(initial.State.Revision == 0 && initial.AvailableDialogues.Single().Id == "DEV.Contact.Offer", "Initial view incorrect.");
    await Reject(() => flow.ChooseAsync(character.CharacterId, owner, Command(0, "Contact.Report", "report"), default), "dialogue_not_available");
    NarrativeCommand accept = Command(0, "Contact.Offer", "accept");
    Task<NarrativeView>[] accepts = Enumerable.Range(0, 4).Select(_ => Task.Run(() =>
        flow.ChooseAsync(character.CharacterId, owner, accept, default))).ToArray();
    NarrativeView[] accepted = await Task.WhenAll(accepts);
    Require(accepted.All(x => x.State.Revision == 1 && x.State.Quests.Single().Status == QuestStatus.Active), "Duplicate accept applied more than once.");
    await Reject(() => flow.ChooseAsync(character.CharacterId, foreign, accept, default), "character_not_owned");
    await Reject(() => flow.ChooseAsync(character.CharacterId, owner, accept with { ChoiceId = "changed" }, default), "idempotency_conflict");
    await Reject(() => flow.ChooseAsync(character.CharacterId, owner, Command(1, "Contact.Explain", "confirm") with { CatalogVersion = 2 }, default), "narrative_catalog_changed");
    await Reject(() => flow.ChooseAsync(character.CharacterId, owner, Command(0, "Contact.Explain", "confirm"), default), "narrative_revision_conflict");
    await Reject(() => flow.ChooseAsync(character.CharacterId, owner, Command(1, "Contact.Explain", "forged"), default), "dialogue_choice_not_available");

    // Distinct concurrent requests share a revision; exactly one commits.
    NarrativeCommand left = Command(1, "Contact.Explain", "confirm"), right = Command(1, "Instructor.Explain", "confirm");
    async Task<bool> Attempt(NarrativeCommand command)
    {
        try { await Coordinator(reopen()).ChooseAsync(character.CharacterId, owner, command, default); return true; }
        catch (NarrativeConflictException error) when (error.Message == "narrative_revision_conflict") { return false; }
    }
    bool[] results = await Task.WhenAll(Task.Run(() => Attempt(left)), Task.Run(() => Attempt(right)));
    Require(results.Count(x => x) == 1, "Concurrent commands overwrote each other.");
    NarrativeView current = await Coordinator(reopen()).LoadAsync(character.CharacterId, owner, default);
    string missing = current.State.Quests.Single().CompletedObjectives.Contains("contact") ? "Instructor.Explain" : "Contact.Explain";
    current = await flow.ChooseAsync(character.CharacterId, owner, Command(2, missing, "confirm"), default);
    Require(current.State.Revision == 3 && current.State.Quests.Single().Status == QuestStatus.ReadyToReport
        && current.State.StoryUnlocks.Length == 0, "Objectives or pre-report unlock incorrect.");
    NarrativeCommand report = Command(3, "Contact.Report", "report");
    NarrativeView done = await flow.ChooseAsync(character.CharacterId, owner, report, default);
    Require(done.State.Revision == 4 && done.State.Quests.Single().Status == QuestStatus.Completed
        && done.State.CompletedChapters.SequenceEqual(["DEV.Prologue"])
        && done.State.StoryUnlocks.SequenceEqual(["DEV.Story.Next"])
        && done.AvailableDialogues.Length == 0, "Completion/chapter/unlock not committed together.");
    Require(Json((await Coordinator(reopen()).LoadAsync(character.CharacterId, owner, default)).State) == Json(done.State), "Reopened coordinator did not restore state.");
    Require(Json((await flow.ChooseAsync(character.CharacterId, owner, report, default)).State) == Json(done.State), "Report replay changed result.");
    Require((await flow.ChooseAsync(character.CharacterId, owner, accept, default)).State.Revision == 1
        && (await flow.LoadAsync(character.CharacterId, owner, default)).State.Revision == 4, "Old receipt replay mutated current state.");
    Require((await flow.LoadAsync(alt.CharacterId, owner, default)).State.Revision == 0, "Progress leaked to alt.");
    Require(growthBefore == Json((await ((ICharacterProgressionRepository)game)
        .LoadProgressionAsync(character.CharacterId, DateTimeOffset.UtcNow, default))!), "Narrative changed combat growth.");
    await Reject(() => flow.ChooseAsync(character.CharacterId, owner, Command(4, "Contact.Offer", "accept"), default), "dialogue_not_available");
    // Failed transaction neither publishes state nor consumes the request ID.
    NarrativeCommand retry = Command(0, "Contact.Offer", "accept");
    await Reject(() => reopen().CommitNarrativeAsync(alt.CharacterId, owner, retry,
        _ => throw new NarrativeConflictException("injected_failure"), DateTimeOffset.UtcNow, default), "injected_failure");
    Require((await flow.ChooseAsync(alt.CharacterId, owner, retry, default)).State.Revision == 1, "Failure consumed command receipt.");
    using CancellationTokenSource canceled = new(); canceled.Cancel();
    try { await flow.ChooseAsync(alt.CharacterId, owner, Command(1, "Contact.Explain", "confirm"), canceled.Token); throw new TestFailure("Cancellation ignored."); }
    catch (OperationCanceledException) { }
    Require((await flow.LoadAsync(alt.CharacterId, owner, default)).State.Revision == 1, "Canceled request changed state.");
    Console.WriteLine($"PASS Narrative {label}: ownership, revisions, concurrent replay/conflict, completion+unlocks, restore, rollback, cancellation, alt/growth isolation.");
}
static async Task HttpCases()
{
    InMemoryTransactionGate gate = new();
    InMemoryGameRepository game = new(gate, new InMemoryItemRepository(gate));
    const string owner = "76561198000000011", foreign = "76561198000000012";
    GameCharacter character = await Character(game, owner);
    await Character(game, foreign);
    AccessTokenService tokens = new(TimeProvider.System);
    IssuedAccessToken ownToken = tokens.Issue(TimeSpan.FromMinutes(5)), foreignToken = tokens.Issue(TimeSpan.FromMinutes(5));
    await game.StoreSessionAsync(ownToken.TokenHash, owner, ownToken.ExpiresAt, default);
    await game.StoreSessionAsync(foreignToken.TokenHash, foreign, foreignToken.ExpiresAt, default);
    WebApplicationBuilder builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
    builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
    builder.Services.AddSingleton<IGameRepository>(game);
    builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
    builder.Services.Configure<AuthOptions>(_ => { }); builder.Services.Configure<SecurityTelemetryOptions>(_ => { });
    builder.Services.AddScoped<BearerAuthenticator>();
    builder.Services.AddSingleton(Coordinator(game));
    await using WebApplication app = builder.Build();
    app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi(); await app.StartAsync();
    try
    {
        string url = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single();
        using HttpClient client = new() { BaseAddress = new Uri(url), Timeout = TimeSpan.FromSeconds(10) };
        string route = $"/api/characters/{character.CharacterId:D}/narrative";
        Require((await client.GetAsync(route)).StatusCode == HttpStatusCode.Unauthorized, "Anonymous HTTP read allowed.");
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", foreignToken.Token);
        Require((await client.GetAsync(route)).StatusCode == HttpStatusCode.Forbidden, "Foreign HTTP read allowed.");
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", ownToken.Token);
        Require((await client.GetAsync(route)).IsSuccessStatusCode, "Owned HTTP read failed.");
        Guid requestId = Guid.NewGuid();
        object command = new { requestId, expectedRevision = "0", catalogVersion = 1, dialogueId = "DEV.Contact.Offer", choiceId = "accept" };
        HttpResponseMessage response = await client.PostAsJsonAsync(route + "/choices", command);
        Require(response.IsSuccessStatusCode && response.Headers.CacheControl?.NoStore == true, "HTTP accept/cache contract failed.");
        using JsonDocument json = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
        Require(json.RootElement.GetProperty("revision").GetString() == "1"
            && json.RootElement.GetProperty("quests")[0].GetProperty("status").GetString() == "Active", "HTTP revision/status contract failed.");
        JsonElement journal = json.RootElement.GetProperty("quests")[0];
        Require(journal.GetProperty("title").GetString() == "출발 전 확인"
            && journal.GetProperty("objectives").GetArrayLength() == 2
            && journal.GetProperty("objectives")[0].GetProperty("text").GetString() == "연락책의 상황 설명 확인"
            && !journal.GetProperty("objectives")[0].GetProperty("completed").GetBoolean(), "HTTP journal metadata/progress mismatch.");
        Require((await client.PostAsJsonAsync(route + "/choices", command)).IsSuccessStatusCode, "HTTP replay failed.");
        response = await client.PostAsJsonAsync(route + "/choices", new { requestId = Guid.NewGuid(), expectedRevision = "0", catalogVersion = 1, dialogueId = "DEV.Contact.Offer", choiceId = "accept" });
        Require(response.StatusCode == HttpStatusCode.Conflict, "Stale HTTP revision accepted.");
        response = await client.PostAsJsonAsync(route + "/choices", new { requestId = Guid.NewGuid(), expectedRevision = "1.5", catalogVersion = 1, dialogueId = "DEV.Contact.Offer", choiceId = "accept" });
        Require(response.StatusCode == HttpStatusCode.BadRequest, "Fractional HTTP revision accepted.");
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", foreignToken.Token);
        Require((await client.PostAsJsonAsync(route + "/choices", command)).StatusCode == HttpStatusCode.Forbidden, "Foreign HTTP receipt replay allowed.");
        Console.WriteLine("PASS Narrative HTTP: actual bearer authentication, owned GET/POST, DTO, replay, 401/403/409/400, no-store.");
    }
    finally { await app.StopAsync(); }
}
static async Task PostgresCases()
{
    string run = Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_RUN") ?? "";
    Require(Guid.TryParseExact(run, "N", out _), "Owned disposable PostgreSQL runner required.");
    NpgsqlConnectionStringBuilder config = new(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_POSTGRES") ?? "");
    Require(config.Host == "127.0.0.1" && config.Database == "narrative_test", "Refusing shared PostgreSQL target.");
    await using NpgsqlDataSource source = NpgsqlDataSource.Create(config.ConnectionString);
    var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
    Require(catalog.Count == 3 && await PostgresSchemaMigrations.ApplyAsync(source, catalog, default) == 3, "Fresh migration failed.");
    Require(await PostgresSchemaMigrations.ApplyAsync(source, catalog, default) == 0, "Migration reapplied.");
    await PostgresSchemaMigrations.ValidateAsync(source, catalog, default);
    PostgresGameRepository Repo() => new(source, NullLogger<PostgresGameRepository>.Instance);
    await RepositoryCases(Repo(), Repo, "PostgreSQL");
    await using NpgsqlDataSource reopened = NpgsqlDataSource.Create(config.ConnectionString);
    var game = new PostgresGameRepository(reopened, NullLogger<PostgresGameRepository>.Instance);
    GameCharacter completed = (await game.GetCharactersAsync("76561198000000001", default))
        .Single(c => Coordinator(game).LoadAsync(c.CharacterId, c.SteamId, default).GetAwaiter().GetResult().State.Revision == 4);
    Require((await Coordinator(game).LoadAsync(completed.CharacterId, completed.SteamId, default)).State.CompletedChapters.Length == 1,
        "Fresh database connection failed to restore completed chapter.");
    Console.WriteLine("PASS Narrative PostgreSQL: migrations v1-v3, no-op replay, validation, reopened connection restoration.");
}
static async Task Reject(Func<Task> action, string code)
{
    try { await action(); }
    catch (NarrativeConflictException error) when (error.Message == code) { return; }
    throw new TestFailure("Expected rejection: " + code);
}
static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
sealed class TestFailure(string message) : Exception(message);
