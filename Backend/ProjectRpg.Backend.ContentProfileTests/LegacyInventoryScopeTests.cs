using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class LegacyInventoryScopeTests
{
    public static async Task Run()
    {
        using ProfileHost host = new("Development");
        using HttpClient http = new() { BaseAddress = await host.StartAsync(), Timeout = TimeSpan.FromSeconds(10) };
        async Task<JsonElement> Send(HttpMethod method, string path, object? body, string? token,
            HttpStatusCode expected, string? failure = null, bool noStore = false)
        {
            using HttpRequestMessage request = new(method, path);
            if (body is not null) request.Content = JsonContent.Create(body);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            using HttpResponseMessage response = await http.SendAsync(request);
            Require(response.StatusCode == expected, failure ?? $"Unexpected legacy inventory status: {(int)response.StatusCode}.");
            if (noStore) Require(response.Headers.CacheControl?.NoStore == true, "Legacy inventory response was cacheable.");
            return expected is HttpStatusCode.OK or HttpStatusCode.Created
                ? await response.Content.ReadFromJsonAsync<JsonElement>() : default;
        }
        async Task<(string Token, string Character, string Session, string ServerToken)> Player(string steam, int port)
        {
            JsonElement login = await Send(HttpMethod.Post, "/api/auth/steam-ticket", new { ticket = "dev:" + steam }, null, HttpStatusCode.OK);
            string token = login.GetProperty("accessToken").GetString()!;
            JsonElement character = await Send(HttpMethod.Post, "/api/characters", new
            {
                name = "Legacy" + Guid.NewGuid().ToString("N")[..8], classId = "RPGPlayerClassDefinition:GreatSwordWarrior"
            }, token, HttpStatusCode.Created);
            string characterId = character.GetProperty("characterId").GetString()!;
            JsonElement session = await Send(HttpMethod.Post, "/api/dungeon-sessions", new
            {
                characterId, dungeonId = "Progression.Training", difficulty = "Normal"
            }, token, HttpStatusCode.Created);
            string id = session.GetProperty("dungeonSessionId").GetString()!;
            await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/departure", new
            {
                characterId, requestId = Guid.NewGuid(), expectedLobbyRevision = session.GetProperty("lobbyRevision").GetInt64()
            }, token, HttpStatusCode.OK);
            JsonElement assigned = await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/activate", new
            {
                serverId = "legacy-shared-label", serverAddress = "127.0.0.1:" + port
            }, host.AdminToken, HttpStatusCode.OK);
            return (token, characterId, id, assigned.GetProperty("gameServerAccessToken").GetString()!);
        }
        object Body(string character, int quantity) => new
        {
            characterId = character, inventory = new[]
            {
                new { item_id = "Potion.Small", quantity, slot_index = 0, category = "Consumable", instance_id = "legacy-scope-" + character }
            }
        };
        string Load(string character) => "/api/loadInventory?characterId=" + character;
        var a = await Player("76561198000000201", 19981);
        var b = await Player("76561198000000202", 19982);
        await Send(HttpMethod.Post, "/api/saveInventory", Body(a.Character, 3), a.ServerToken, HttpStatusCode.NoContent);
        await Send(HttpMethod.Post, "/api/saveInventory", Body(b.Character, 7), b.ServerToken, HttpStatusCode.NoContent);
        JsonElement beforeA = await Send(HttpMethod.Get, Load(a.Character), null, a.Token, HttpStatusCode.OK);
        JsonElement beforeB = await Send(HttpMethod.Get, Load(b.Character), null, b.Token, HttpStatusCode.OK);

        // Old code accepts any active game-server token and returns 204 here.
        await Send(HttpMethod.Post, "/api/saveInventory", Body(b.Character, 99), a.ServerToken, HttpStatusCode.Forbidden,
            "Foreign dungeon server overwrote legacy inventory.", noStore: true);
        await Send(HttpMethod.Get, Load(b.Character), null, a.ServerToken, HttpStatusCode.Forbidden, noStore: true);
        await Send(HttpMethod.Post, "/api/saveInventory", Body(a.Character, 99), b.ServerToken, HttpStatusCode.Forbidden, noStore: true);
        await Send(HttpMethod.Get, Load(a.Character), null, b.ServerToken, HttpStatusCode.Forbidden, noStore: true);
        Console.WriteLine("PASS legacy HTTP: same server label cannot read or overwrite another session's inventory");

        JsonElement sibling = await Send(HttpMethod.Post, "/api/characters", new
        {
            name = "Sibling" + Guid.NewGuid().ToString("N")[..8], classId = "RPGPlayerClassDefinition:GreatSwordWarrior"
        }, a.Token, HttpStatusCode.Created);
        string siblingId = sibling.GetProperty("characterId").GetString()!;
        await Send(HttpMethod.Get, Load(siblingId), null, a.ServerToken, HttpStatusCode.Forbidden, noStore: true);
        await Send(HttpMethod.Post, "/api/saveInventory", Body(siblingId, 99), a.ServerToken, HttpStatusCode.Forbidden, noStore: true);
        await Send(HttpMethod.Get, Load(siblingId), null, a.Token, HttpStatusCode.OK, noStore: true);
        foreach (var player in new[] { a, b })
        {
            await Send(HttpMethod.Get, Load(player.Character), null, player.ServerToken, HttpStatusCode.OK, noStore: true);
            await Send(HttpMethod.Get, Load(player.Character), null, player.Token, HttpStatusCode.OK, noStore: true);
            await Send(HttpMethod.Post, "/api/saveInventory", Body(player.Character, 99), player.Token, HttpStatusCode.Forbidden, noStore: true);
        }
        await Send(HttpMethod.Get, Load(a.Character), null, b.Token, HttpStatusCode.Forbidden, noStore: true);
        await Send(HttpMethod.Post, "/api/saveInventory", Body(a.Character, 99), host.AdminToken, HttpStatusCode.Forbidden, noStore: true);
        await Send(HttpMethod.Get, Load(a.Character), null, null, HttpStatusCode.Unauthorized);
        Console.WriteLine("PASS legacy HTTP: sibling without membership denied; own server/player reads and player write denial retained");

        await Send(HttpMethod.Post, "/api/saveInventory", Body(a.Character, 0), a.ServerToken, HttpStatusCode.BadRequest, noStore: true);
        Require(beforeA.GetRawText() == (await Send(HttpMethod.Get, Load(a.Character), null, a.Token, HttpStatusCode.OK)).GetRawText()
            && beforeB.GetRawText() == (await Send(HttpMethod.Get, Load(b.Character), null, b.Token, HttpStatusCode.OK)).GetRawText(),
            "A rejected request changed an inventory.");
        await Send(HttpMethod.Post, $"/api/dungeon-sessions/{a.Session}/finish", new
        {
            serverId = "legacy-shared-label", outcome = "Failed"
        }, a.ServerToken, HttpStatusCode.OK);
        await Send(HttpMethod.Get, Load(a.Character), null, a.ServerToken, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Post, "/api/saveInventory", Body(a.Character, 99), a.ServerToken, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Get, Load(a.Character), null, a.Token, HttpStatusCode.OK, noStore: true);
        Require(beforeB.GetRawText() == (await Send(HttpMethod.Get, Load(b.Character), null, b.ServerToken, HttpStatusCode.OK)).GetRawText(),
            "Terminating one server changed another member's inventory.");
        Console.WriteLine("PASS legacy HTTP: validation/denials preserve data; finished server loses access without affecting owner or peer");
    }

    public static async Task Storage()
    {
        await CheckRepository("Memory", new InMemoryGameRepository());
        string? connection = Environment.GetEnvironmentVariable("PROJECT_RPG_LEGACY_TEST_POSTGRES");
        Require(!string.IsNullOrWhiteSpace(connection), "An owned PostgreSQL test database is required.");
        await using NpgsqlDataSource source = NpgsqlDataSource.Create(connection!);
        PostgresGameRepository repository = new(source, NullLogger<PostgresGameRepository>.Instance);
        await repository.EnsureCreatedAsync(default);
        try
        {
            await CheckRepository("Postgres", repository);
            await CheckPostgresSerialization(source, repository);
        }
        catch (PostgresException error)
        {
            // Do not print connection strings, row contents or credentials.
            throw new TestFailure($"Legacy PostgreSQL: {error.SqlState}/{error.ConstraintName}/{error.Routine}.");
        }
        catch (InvalidOperationException error)
        {
            throw new TestFailure("Legacy PostgreSQL fixture: " + error.Message);
        }
    }

    private static async Task CheckRepository(string provider, IGameRepository repository)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        async Task<GameCharacter> Character(string steam)
        {
            await repository.UpsertAccountAsync(new SteamIdentity(steam, steam, false, false), now, default);
            return await repository.CreateCharacterAsync(steam, "Legacy" + Guid.NewGuid().ToString("N")[..8], now, default);
        }
        GameCharacter a = await Character("76561198000000301");
        GameCharacter sibling = await Character(a.SteamId);
        GameCharacter b = await Character("76561198000000302");
        GameCharacter mate = await Character("76561198000000303");
        DungeonSession sessionA = await repository.CreateDungeonSessionAsync(a.SteamId, a.CharacterId,
            "Dungeon.LegacyScope", "Normal", now, now.AddHours(1), default);
        await repository.JoinDungeonSessionAsync(sessionA.DungeonSessionId, mate.SteamId, mate.CharacterId,
            4, now, now.AddHours(1), default);
        DungeonSession sessionB = await repository.CreateDungeonSessionAsync(b.SteamId, b.CharacterId,
            "Dungeon.LegacyScope", "Normal", now, now.AddHours(1), default);
        foreach (DungeonSession session in new[] { sessionA, sessionB })
            await repository.AssignDungeonServerAsync(session.DungeonSessionId, "shared-label", "127.0.0.1:7777",
                now, now.AddHours(1), default);
        GameServerCredential scopeA = new("shared-label", sessionA.DungeonSessionId);
        InventoryItem[] original = [new("Potion.Small", 3, 0, "Consumable", "original-" + a.CharacterId)];
        InventoryItem[] replacement = [new("Potion.Small", 99, 0, "Consumable", "replacement")];
        await repository.SaveInventoryAsync(a.CharacterId, original, now, default, scopeA);
        await repository.StartDungeonSessionAsync(sessionA.DungeonSessionId, "shared-label", now, now.AddHours(1), default);
        await repository.SaveInventoryAsync(a.CharacterId, original, now, default, scopeA);
        async Task Deny(Guid character, GameServerCredential scope, DateTimeOffset at)
        {
            bool denied = false;
            try { await repository.SaveInventoryAsync(character, replacement, at, default, scope); }
            catch (DungeonSessionMembershipException) { denied = true; }
            Require(denied, "Repository accepted a foreign/revoked legacy inventory authority.");
        }
        await Deny(a.CharacterId, new("shared-label", sessionB.DungeonSessionId), now);
        await Deny(a.CharacterId, new("different-label", sessionA.DungeonSessionId), now);
        await Deny(a.CharacterId, new("shared-label", Guid.NewGuid()), now);
        await Deny(a.CharacterId, scopeA with { IsSecurityTelemetryOnly = true }, now);
        await Deny(a.CharacterId, scopeA, now.AddHours(2));
        await Deny(sibling.CharacterId, scopeA, now);
        Require((await repository.LoadInventoryAsync(a.CharacterId, default)).SequenceEqual(original)
            && (await repository.LoadInventoryAsync(sibling.CharacterId, default)).Count == 0,
            "Denied scoped replacement deleted or changed stored items.");
        Console.WriteLine($"PASS legacy storage: {provider} permits assigned Loading/InProgress writes; rejects foreign scope, expiry and unassigned sibling");

        await repository.AbandonDungeonSessionAsync(sessionA.DungeonSessionId, a.SteamId, a.CharacterId, now, default);
        await Deny(a.CharacterId, scopeA, now);
        InventoryItem[] mateItems = [original[0] with { InstanceId = "original-" + mate.CharacterId }];
        await repository.SaveInventoryAsync(mate.CharacterId, mateItems, now, default, scopeA);
        Require((await repository.LoadInventoryAsync(a.CharacterId, default)).SequenceEqual(original)
            && (await repository.LoadInventoryAsync(mate.CharacterId, default)).SequenceEqual(mateItems),
            "Membership loss damaged the old inventory or remaining member's authority.");
        Console.WriteLine($"PASS legacy storage: {provider} membership removal fences the departed character but preserves remaining member");
    }

    private static async Task CheckPostgresSerialization(NpgsqlDataSource source, IGameRepository repository)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        const string steam = "76561198000000401";
        await repository.UpsertAccountAsync(new SteamIdentity(steam, steam, false, false), now, default);
        GameCharacter character = await repository.CreateCharacterAsync(steam, "Lock" + Guid.NewGuid().ToString("N")[..8], now, default);
        DungeonSession session = await repository.CreateDungeonSessionAsync(steam, character.CharacterId,
            "Dungeon.LegacyScope", "Normal", now, now.AddHours(1), default);
        await repository.AssignDungeonServerAsync(session.DungeonSessionId, "serialization", "127.0.0.1:7778",
            now, now.AddHours(1), default);
        GameServerCredential scope = new("serialization", session.DungeonSessionId);
        InventoryItem[] original = [new("Potion.Small", 7, 0, "Consumable", "original-" + character.CharacterId)];
        await repository.SaveInventoryAsync(character.CharacterId, original, now, default, scope);
        // A failed INSERT after DELETE must roll back the original inventory.
        bool rolledBack = false;
        try { await repository.SaveInventoryAsync(character.CharacterId, [original[0], original[0]], now, default, scope); }
        catch (PostgresException error) when (error.SqlState == PostgresErrorCodes.UniqueViolation) { rolledBack = true; }
        Require(rolledBack && (await repository.LoadInventoryAsync(character.CharacterId, default)).SequenceEqual(original),
            "Legacy replacement failed to roll back DELETE after a rejected INSERT.");

        await using NpgsqlConnection blocker = await source.OpenConnectionAsync();
        await using NpgsqlTransaction transaction = await blocker.BeginTransactionAsync();
        await using (NpgsqlCommand finish = new("UPDATE dungeon_sessions SET state = 'Failed' WHERE dungeon_session_id = @session;", blocker, transaction))
        {
            finish.Parameters.AddWithValue("session", session.DungeonSessionId);
            await finish.ExecuteNonQueryAsync();
        }
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
        Task save = repository.SaveInventoryAsync(character.CharacterId,
            [new("Potion.Small", 99, 0, "Consumable", "late")], now, timeout.Token, scope);
        bool committed = false;
        try
        {
            bool waiting = false;
            for (int attempt = 0; attempt < 100 && !save.IsCompleted; attempt++)
            {
                await using NpgsqlConnection observation = await source.OpenConnectionAsync(timeout.Token);
                await using NpgsqlCommand query = new("""
                    SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE datname = current_database()
                        AND wait_event_type = 'Lock' AND query LIKE '%SELECT state, server_id, server_address%');
                    """, observation);
                waiting = (bool)(await query.ExecuteScalarAsync(timeout.Token))!;
                if (waiting) break;
                await Task.Delay(20, timeout.Token);
            }
            Require(waiting, "Scoped save did not serialize on the dungeon parent row.");
            await transaction.CommitAsync(timeout.Token);
            committed = true;
            bool denied = false;
            try { await save; } catch (DungeonSessionMembershipException) { denied = true; }
            Require(denied && (await repository.LoadInventoryAsync(character.CharacterId, default)).SequenceEqual(original),
                "Save used stale authorization after a concurrent terminal transition.");
        }
        finally
        {
            if (!committed) await transaction.RollbackAsync();
            timeout.Cancel();
            try { await save; } catch (Exception error) when (error is OperationCanceledException or DungeonSessionMembershipException) { }
        }
        Console.WriteLine("PASS legacy storage: Postgres INSERT failure rolls back; blocked save rechecks authority after terminal transition");
    }

    private static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
