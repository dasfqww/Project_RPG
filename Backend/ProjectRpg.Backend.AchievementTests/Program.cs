using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Achievements;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

var pg = Environment.GetEnvironmentVariable("RPG_ACHIEVEMENTS_TEST_DATABASE");
await using var source = pg is null ? null : NpgsqlDataSource.Create(pg);
if (source is not null)
{
    await using var guard = source.CreateCommand("SELECT current_database(), to_regclass('characters') IS NULL");
    await using var reader = await guard.ExecuteReaderAsync();
    await reader.ReadAsync();
    if (reader.GetString(0) != "achievements_test" || !reader.GetBoolean(1))
        throw new InvalidOperationException("Expected an empty disposable achievements_test database.");
}
var catalog = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
if (source is not null) await PostgresSchemaMigrations.ApplyAsync(source, catalog.Take(17).ToArray(), default);
IGameRepository game = source is null ? new InMemoryGameRepository() :
    new PostgresGameRepository(source, NullLogger<PostgresGameRepository>.Instance);
var now = new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);
const string owner = "76561198001231000", stranger = "76561198001231001", server = "achievement-test", dungeon = "Progression.FirstBoss";
foreach (var account in new[] { owner, stranger })
    await game.UpsertAccountAsync(new(account, account, false, false), now, default);
var main = await game.CreateCharacterAsync(owner, "Main", now, default);
var alt = await game.CreateCharacterAsync(owner, "Alt", now, default);
var foreign = await game.CreateCharacterAsync(stranger, "Foreign", now, default);
async Task Clear(GameCharacter actor, string difficulty, string dungeonId = dungeon, bool includeAlt = false)
{
    var until = now.AddHours(1);
    var session = await game.CreateDungeonSessionAsync(owner, actor.CharacterId, dungeonId, difficulty, now, until, default);
    var id = session.DungeonSessionId;
    if (includeAlt) await game.JoinDungeonSessionAsync(id, owner, alt.CharacterId, 4, now, until, default);
    await game.AssignDungeonServerAsync(id, server, "127.0.0.1:7777", now, until, default);
    await game.StartDungeonSessionAsync(id, server, now, until, default);
    Task<DungeonRewardSettlement> Report() => game.EnqueueDungeonRewardSettlementAsync(id, server,
        "achievement-test", new string('a', 64), [], [], now, default);
    await Task.WhenAll(Report(), Report());
    await game.ClaimNextDungeonRewardSettlementAsync(server, now, until, default);
    await game.CompleteDungeonRewardSettlementAsync(id, server, now, default);
    now = now.AddHours(1);
}
async Task ReportChapter(GameCharacter actor, bool skip)
{
    var repository = (INarrativeRepository)game;
    NarrativeCommand command = new(Guid.NewGuid(), 0, 1, "CH01.M08.Report", "report");
    NarrativeState Transition(NarrativeState state) => state with
    {
        Revision = 1, CompletedChapters = ["CH01"],
        Quests = skip ? [] : [.. Enumerable.Range(1, 8).Select(i => new QuestProgress($"CH01.M{i:00}", 1, QuestStatus.Completed, []))],
        StorySkips = skip ? [new("CH01", 1, now, ["CH01.M08"], new string('a', 64))] : []
    };
    await repository.CommitNarrativeAsync(actor.CharacterId, actor.SteamId, command, Transition, now, default);
    await repository.CommitNarrativeAsync(actor.CharacterId, actor.SteamId, command, Transition, now.AddHours(1), default);
}
await Clear(main, "Hard");
await Clear(main, "Normal", "Progression.Intro1");
var first = now;
for (int i = 0; i < 9; ++i) await Clear(main, "Normal", includeAlt: i == 0);
await ReportChapter(foreign, true);
var chapterTime = now;
await ReportChapter(main, false);
if (source is not null)
{
    await PostgresSchemaMigrations.ApplyAsync(source, catalog, default);
    await game.EnsureCreatedAsync(default); // Development initialization replays the catalog.
}
async Task<AchievementSnapshot> Read(GameCharacter c) =>
    await ((IAchievementRepository)game).ReadAchievementsAsync(c.CharacterId, c.SteamId, default) ?? throw new Exception("missing snapshot");
void Check(bool result, string description)
{
    if (!result) throw new InvalidOperationException(description);
    Console.WriteLine("PASS " + description);
}
var before = await Read(alt);
AchievementEntry Find(AchievementSnapshot snapshot, string id) => snapshot.Entries.Single(e => e.Id == id);
Check(before.RosterId == main.RosterId && Find(before,"mine.first").EarnedAt == first &&
    Find(before,"mine.ten").Progress == 9 && Find(before,"mine.ten").EarnedAt is null, "nine distinct Normal receipts, first historical date, alt sharing");
Check(Find(before,"chapter.one").EarnedAt == chapterTime && (await Read(foreign)).Entries.All(e => e.Progress == 0),
    "direct report historical date, skip and foreign roster excluded");
Check(await ((IAchievementRepository)game).ReadAchievementsAsync(main.CharacterId, stranger, default) is null, "foreign character denied");
var direct = await game.CreateCharacterAsync(stranger, "Reporter", now, default);
await ReportChapter(direct, false);
Check(Find(await Read(foreign), "chapter.one").EarnedAt == now, "live direct report on another character unlocks chapter for roster");
var tenth = now;
await Clear(alt, "Normal");
await Clear(main, "Normal");
if (source is not null) game = new PostgresGameRepository(source, NullLogger<PostgresGameRepository>.Instance);
var after = await Read(main);
Check(Find(after,"mine.ten").Progress == 10 && Find(after,"mine.ten").EarnedAt == tenth &&
    Find(after,"mine.first").EarnedAt == first && Find(after,"chapter.one").EarnedAt == chapterTime,
    "tenth clear on alt unlocks once; subsequent clears and repository reopen preserve dates");
if (source is not null)
{
    var fresh = await game.CreateCharacterAsync(owner, "OtherWorld", now, default);
    await using var move = source.CreateCommand("""
        INSERT INTO rosters(roster_id, steam_id, world_id, created_at, updated_at) VALUES(@roster, @owner, 'other', @now, @now);
        UPDATE characters SET roster_id=@roster WHERE character_id=@id;
        """);
    move.Parameters.AddWithValue("roster", Guid.NewGuid()); move.Parameters.AddWithValue("owner", owner);
    move.Parameters.AddWithValue("now", now); move.Parameters.AddWithValue("id", fresh.CharacterId);
    await move.ExecuteNonQueryAsync();
    Check((await Read(fresh)).Entries.All(e => e.Progress == 0), "same account on another world isolated");
}
