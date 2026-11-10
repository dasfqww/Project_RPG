using System.Collections.Immutable;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
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
    await Cases(memory, () => memory, null, "Memory");
    if (args.Length != 0)
    {
        Check(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_RUN"), "N", out _), "Owned runner required");
        var config = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_POSTGRES") ?? "");
        Check(config.Host == "127.0.0.1" && config.Database == "narrative_test", "Private database required");
        await using var source = NpgsqlDataSource.Create(config.ConnectionString);
        var migrations = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
        await PostgresSchemaMigrations.ApplyAsync(source, migrations, default);
        await PostgresSchemaMigrations.ValidateAsync(source, migrations, default);
        PostgresGameRepository Repo() => new(source, NullLogger<PostgresGameRepository>.Instance);
        await Cases(Repo(), () => Repo(), source, "Postgres/reopened repositories");
    }
    Console.WriteLine("PASS account story history groups"); return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine("FAIL history: " + (error is Failure or NarrativeConflictException ? error.Message
        : error is PostgresException pg ? "SQLSTATE " + pg.SqlState : error.GetType().Name));
    Console.Error.WriteLine(error.StackTrace); return 1;
}

static NarrativeCatalog Catalog(int version = 11)
{
    var quests = new[] { "Q.Z", "Q.A", "Q.Foreign" }.Select(id => new QuestDefinition(id, version == 11 ? 1 : 2, [], ["talk"]));
    return new(version, quests,
        [new("Chapter.Z", ["Q.Z"], ["Story.Z"]), new("Chapter.A1", ["Q.A"], ["Story.A1"]),
         new("Chapter.A2", ["Q.A"], ["Story.A2"]), new("Chapter.Foreign", ["Q.Foreign"], ["Story.Foreign"])],
        quests.SelectMany(q => new DialogueDefinition[] {
            new(q.Id+".Offer", "NPC", "Offer", q.Id, null, [new("accept", "Accept", DialogueEffect.AcceptQuest)]),
            new(q.Id+".Talk", "NPC", "Talk", q.Id, QuestStatus.Active, [new("talk", "Talk", DialogueEffect.RecordObjective, "talk")]),
            new(q.Id+".Report", "NPC", "Report", q.Id, QuestStatus.ReadyToReport, [new("report", "Report", DialogueEffect.ReportQuest)]) }));
}
static async Task Cases(IGameRepository game, Func<INarrativeHistoryRepository> reopen, NpgsqlDataSource? source, string label)
{
    var clock = TimeProvider.System; var catalog = Catalog(); var repository = (INarrativeRepository)game;
    var commands = new NarrativeCoordinator(repository, catalog, new(), new(), new(new()), clock);
    var history = new NarrativeAccountHistory(reopen(), catalog);
    async Task<GameCharacter> Character(string owner)
    {
        await game.UpsertAccountAsync(new(owner, owner, false, false), clock.GetUtcNow(), default);
        return await game.CreateCharacterAsync(owner, "H"+Guid.NewGuid().ToString("N")[..12], clock.GetUtcNow(), default);
    }
    const string owner = "76561198000009310", stranger = "76561198000009311";
    var hero = await Character(owner); var alt = await Character(owner); var second = await Character(owner);
    var blank = await Character(owner); var foreign = await Character(stranger);
    async Task Choose(GameCharacter character, string quest, string node, string choice)
    {
        var state = await repository.LoadNarrativeAsync(character.CharacterId, character.SteamId, default);
        await commands.ChooseAsync(character.CharacterId, character.SteamId,
            new(Guid.NewGuid(), state.Revision, catalog.Version, quest+"."+node, choice), default);
    }
    async Task Complete(GameCharacter character, string quest)
    {
        await Choose(character, quest, "Offer", "accept"); await Choose(character, quest, "Talk", "talk"); await Choose(character, quest, "Report", "report");
    }
    Check((await history.LoadAsync(alt.CharacterId, owner, default)).Chapters.IsEmpty, "Fresh account has history");
    await Choose(hero, "Q.Z", "Offer", "accept"); await Choose(hero, "Q.Z", "Talk", "talk");
    Check((await history.LoadAsync(alt.CharacterId, owner, default)).Chapters.IsEmpty, "Unreported goal counted as completion");
    await Choose(hero, "Q.Z", "Report", "report"); await Complete(foreign, "Q.Foreign");
    var initial = await history.LoadAsync(alt.CharacterId, owner, default);
    Check(initial.CharacterId == alt.CharacterId && initial.CatalogVersion == 11 && initial.Chapters.SequenceEqual(
        [new AccountChapterHistory("Chapter.Z", false, true)]), "Other character's direct completion not projected");
    Console.WriteLine("PASS "+label+": reported completion only, foreign account excluded");

    var writer = Task.Run(() => Complete(second, "Q.A"));
    var reads = Enumerable.Range(0, 24).Select(_ => Task.Run(async () =>
    {
        var view = await new NarrativeAccountHistory(reopen(), catalog).LoadAsync(alt.CharacterId, owner, default);
        Check(view.Chapters.Any(c => c.ChapterId == "Chapter.A1") == view.Chapters.Any(c => c.ChapterId == "Chapter.A2"), "Torn chapter projection");
    }));
    await Task.WhenAll(reads.Append(writer));
    await Complete(hero, "Q.A");
    var direct = await history.LoadAsync(hero.CharacterId, owner, default);
    Check(direct.Chapters.SequenceEqual([new("Chapter.A1", true, true), new("Chapter.A2", true, true), new("Chapter.Z", true, false)]), "Stable union/dedup/selected flags failed");
    var titled = new NarrativeCatalog(catalog.Version, catalog.Quests.Values,
        catalog.Chapters.Values.Select(c => c with { Title = c.Id == "Chapter.Z" ? "서장" : null }), catalog.Dialogues.Values);
    Check((await new NarrativeAccountHistory(reopen(), titled).LoadAsync(hero.CharacterId, owner, default))
        .Chapters.Single(c => c.ChapterId == "Chapter.Z").Title == "서장", "Cosmetic chapter title lost");
    foreach (var title in new[] { "", "   ", new string('x', 257) })
    {
        try { _ = new NarrativeCatalog(11, catalog.Quests.Values, catalog.Chapters.Values.Select(c => c with { Title = title }), catalog.Dialogues.Values); throw new Failure("Invalid title accepted"); }
        catch (ArgumentException e) when (e.Message == "invalid_chapter_definition") { }
    }
    var before = await repository.LoadNarrativeAsync(alt.CharacterId, owner, default);
    var growth = JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(alt.CharacterId, clock.GetUtcNow(), default));
    string? fingerprint = source is null ? null : await Fingerprint(source);
    var blankView = await new NarrativeAccountHistory(reopen(), catalog).LoadAsync(blank.CharacterId, owner, default);
    Check(blankView.Chapters.Select(c => c.ChapterId).SequenceEqual(["Chapter.A1", "Chapter.A2", "Chapter.Z"])
        && blankView.Chapters.All(c => !c.CompletedOnSelectedCharacter && c.CompletedOnOtherCharacter), "Empty narrative row handling failed");
    await Reject(() => history.LoadAsync(foreign.CharacterId, owner, default), "character_not_owned");
    await Reject(() => history.LoadAsync(Guid.NewGuid(), owner, default), "character_not_owned");
    using (var cancellation = new CancellationTokenSource())
    {
        cancellation.Cancel();
        try { await history.LoadAsync(alt.CharacterId, owner, cancellation.Token); throw new Failure("Cancellation ignored"); }
        catch (OperationCanceledException) { }
    }
    await Http(game, titled, hero, alt, foreign);
    Check(JsonSerializer.Serialize(before) == JsonSerializer.Serialize(await repository.LoadNarrativeAsync(alt.CharacterId, owner, default)), "History changed selected progress");
    Check(before.Revision == 0 && before.Quests.IsEmpty && before.StoryUnlocks.IsEmpty && before.CompletedChapters.IsEmpty, "Account completion became selected unlocks");
    Check(growth == JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(alt.CharacterId, clock.GetUtcNow(), default)), "History changed growth");
    if (source is not null) Check(fingerprint == await Fingerprint(source), "Read path wrote narrative/receipts/progression");
    await Reject(() => new NarrativeAccountHistory(reopen(), Catalog(12)).LoadAsync(alt.CharacterId, owner, default), "incompatible_narrative_state");
    Console.WriteLine("PASS "+label+": coherent concurrent snapshots, reopened reads, cancellation, ownership, no writes/unlocks/growth");

    var foreignBad = await Character(stranger);
    await SeedFlag(foreignBad);
    Check((await history.LoadAsync(alt.CharacterId, owner, default)).Chapters.Length == 3, "Foreign corrupt state affected own account");
    await Reject(() => history.LoadAsync(foreignBad.CharacterId, owner, default), "character_not_owned");
    var broken = await Character(owner); await SeedFlag(broken);
    var badBefore = await repository.LoadNarrativeAsync(broken.CharacterId, owner, default);
    await Reject(() => history.LoadAsync(alt.CharacterId, owner, default), "inconsistent_story_unlocks");
    Check(JsonSerializer.Serialize(badBefore) == JsonSerializer.Serialize(await repository.LoadNarrativeAsync(broken.CharacterId, owner, default)), "Corruption was reset/projected on read");
    if (source is not null)
    {
        await using var connection = await source.OpenConnectionAsync();
        await using var query = new NpgsqlCommand("UPDATE character_narrative SET state = jsonb_set(state, '{characterId}', to_jsonb(@wrong::text)) WHERE character_id = @id;", connection);
        query.Parameters.AddWithValue("wrong", Guid.NewGuid().ToString()); query.Parameters.AddWithValue("id", broken.CharacterId);
        await query.ExecuteNonQueryAsync();
        string corrupted = await Fingerprint(source);
        await Reject(() => new NarrativeAccountHistory(reopen(), catalog).LoadAsync(alt.CharacterId, owner, default), "invalid_narrative_state");
        Check(corrupted == await Fingerprint(source), "Corrupt JSON identity was rewritten");
    }
    Console.WriteLine("PASS "+label+": incompatible/forged completion rejected; foreign corruption isolated; corrupt storage preserved");

    async Task SeedFlag(GameCharacter character) => _ = await repository.CommitNarrativeAsync(character.CharacterId, character.SteamId,
        new(Guid.NewGuid(), 0, 11, "fixture", "seed"), s => s with { Revision = 1, CompletedChapters = ["Chapter.A1"], StoryUnlocks = ["Story.A1"] }, clock.GetUtcNow(), default);
}
static async Task<string> Fingerprint(NpgsqlDataSource source)
{
    await using var connection = await source.OpenConnectionAsync();
    await using var query = new NpgsqlCommand("""
        SELECT jsonb_build_object(
          'states',(SELECT coalesce(jsonb_agg(to_jsonb(n) ORDER BY character_id),'[]'::jsonb) FROM character_narrative n),
          'receipts',(SELECT coalesce(jsonb_agg(to_jsonb(n) ORDER BY character_id,request_id),'[]'::jsonb) FROM character_narrative_commands n),
          'growth',(SELECT coalesce(jsonb_agg(to_jsonb(p) ORDER BY character_id),'[]'::jsonb) FROM character_progression p))::text;
        """, connection);
    return (string)(await query.ExecuteScalarAsync())!;
}
static async Task Http(IGameRepository game, NarrativeCatalog catalog, GameCharacter hero, GameCharacter alt, GameCharacter foreign)
{
    var tokens = new AccessTokenService(TimeProvider.System); var player = tokens.Issue(TimeSpan.FromMinutes(10)); var server = tokens.Issue(TimeSpan.FromMinutes(10));
    await game.StoreSessionAsync(player.TokenHash, alt.SteamId, player.ExpiresAt, default);
    var session = await game.CreateDungeonSessionAsync(alt.SteamId, alt.CharacterId, "test_dungeon", "Normal", DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddHours(1), default);
    await game.AssignDungeonServerAsync(session.DungeonSessionId, "history-test", "127.0.0.1:7777", DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddHours(1), default);
    await game.StartDungeonSessionAsync(session.DungeonSessionId, "history-test", DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddHours(1), default);
    await game.StoreGameServerCredentialAsync(server.TokenHash, "history-test", session.DungeonSessionId, server.ExpiresAt, default);
    string admin = Guid.NewGuid().ToString("N");
    var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
    builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
    builder.Services.AddSingleton<IGameRepository>(game); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
    builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
    builder.Services.Configure<SecurityTelemetryOptions>(_ => { }); builder.Services.AddScoped<BearerAuthenticator>();
    builder.Services.AddSingleton(new NarrativeAccountHistory((INarrativeHistoryRepository)game, catalog));
    builder.Services.AddSingleton(new NarrativeCoordinator((INarrativeRepository)game, catalog, new(), new(), new(new()), TimeProvider.System));
    await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapNarrativeApi(); await app.StartAsync();
    try
    {
        using var client = new HttpClient { BaseAddress = new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
        string route = $"/api/characters/{alt.CharacterId}/narrative/account-history";
        Check((await client.GetAsync(route)).StatusCode == HttpStatusCode.Unauthorized, "Anonymous history accepted");
        foreach (var token in new[] { admin, server.Token })
        {
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
            using var deniedRole = await client.GetAsync(route);
            Check(deniedRole.StatusCode == HttpStatusCode.Forbidden, "Non-player history gate returned "+deniedRole.StatusCode);
        }
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", player.Token);
        using var response = await client.GetAsync(route);
        Check(response.StatusCode == HttpStatusCode.OK && response.Headers.CacheControl?.NoStore == true, "Player history/no-store failed");
        var body = await response.Content.ReadFromJsonAsync<JsonElement>();
        Check(body.GetProperty("characterId").GetGuid() == alt.CharacterId && body.GetProperty("catalogVersion").GetInt32() == 11
            && body.EnumerateObject().Count() == 3 && body.GetProperty("chapters").GetArrayLength() == 3, "History DTO shape failed");
        Check(!body.ToString().Contains(hero.CharacterId.ToString()) && !body.ToString().Contains(foreign.CharacterId.ToString())
            && !body.ToString().Contains(alt.SteamId) && !body.ToString().Contains("Story."), "History leaks identities/unlocks");
        Check(body.GetProperty("chapters").EnumerateArray().Single(c => c.GetProperty("chapterId").GetString() == "Chapter.Z")
            .GetProperty("title").GetString() == "서장", "HTTP chapter title missing");
        foreach (var id in new[] { foreign.CharacterId, Guid.NewGuid() })
        {
            using var denied = await client.GetAsync(route.Replace(alt.CharacterId.ToString(), id.ToString()));
            Check(denied.StatusCode == HttpStatusCode.Forbidden && denied.Headers.CacheControl?.NoStore == true, "Foreign/missing character disclosed");
        }
        Check((await client.PostAsJsonAsync(route, new { })).StatusCode == HttpStatusCode.MethodNotAllowed, "History endpoint is mutable");
        Console.WriteLine("PASS authenticated HTTP: player-only, no-store, minimal private chapter DTO, foreign/missing denied, GET only");
    }
    finally { await app.StopAsync(); }
}
static async Task Reject(Func<Task> action, string code)
{
    try { await action(); throw new Failure("Expected rejection: "+code); }
    catch (NarrativeConflictException error) when (error.Message == code) { }
}
static void Check(bool condition, string message) { if (!condition) throw new Failure(message); }
sealed class Failure(string message) : Exception(message);
