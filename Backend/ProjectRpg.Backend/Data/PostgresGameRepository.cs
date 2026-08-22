using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class PostgresGameRepository(
    NpgsqlDataSource dataSource,
    ILogger<PostgresGameRepository> logger) : IGameRepository
{
    private static readonly JsonSerializerOptions SettlementJsonOptions =
        new(JsonSerializerDefaults.Web);

    public async Task EnsureCreatedAsync(CancellationToken cancellationToken)
    {
        string schemaPath = Path.Combine(AppContext.BaseDirectory, "Database", "schema.sql");
        if (!File.Exists(schemaPath))
        {
            throw new FileNotFoundException("PostgreSQL schema file is missing.", schemaPath);
        }

        string schema = await File.ReadAllTextAsync(schemaPath, cancellationToken);
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(schema, connection);
        await command.ExecuteNonQueryAsync(cancellationToken);
        logger.LogInformation("PostgreSQL schema is ready.");
    }

    public async Task UpsertAccountAsync(
        SteamIdentity identity,
        DateTimeOffset authenticatedAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            WITH authenticated_account AS (
                INSERT INTO accounts (
                    steam_id, owner_steam_id, vac_banned, publisher_banned,
                    created_at, last_login_at)
                VALUES (
                    @steam_id, @owner_steam_id, @vac_banned, @publisher_banned,
                    @authenticated_at, @authenticated_at)
                ON CONFLICT (steam_id) DO UPDATE SET
                    owner_steam_id = EXCLUDED.owner_steam_id,
                    vac_banned = EXCLUDED.vac_banned,
                    publisher_banned = EXCLUDED.publisher_banned,
                    last_login_at = EXCLUDED.last_login_at
                RETURNING steam_id
            )
            INSERT INTO rosters (
                roster_id, steam_id, world_id, created_at, updated_at)
            SELECT
                @roster_id, steam_id, 'main', @authenticated_at, @authenticated_at
            FROM authenticated_account
            ON CONFLICT (steam_id, world_id) DO UPDATE SET
                updated_at = EXCLUDED.updated_at;
            """;

        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("steam_id", identity.SteamId);
        command.Parameters.AddWithValue("owner_steam_id", identity.OwnerSteamId);
        command.Parameters.AddWithValue("vac_banned", identity.VacBanned);
        command.Parameters.AddWithValue("publisher_banned", identity.PublisherBanned);
        command.Parameters.AddWithValue("authenticated_at", authenticatedAt);
        command.Parameters.AddWithValue("roster_id", Guid.NewGuid());
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    public async Task StoreSessionAsync(
        string tokenHash,
        string steamId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            INSERT INTO auth_sessions (token_hash, steam_id, expires_at)
            VALUES (@token_hash, @steam_id, @expires_at)
            ON CONFLICT (token_hash) DO UPDATE SET
                steam_id = EXCLUDED.steam_id,
                expires_at = EXCLUDED.expires_at;
            """;

        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("expires_at", expiresAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    public async Task<string?> ResolveSessionAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT steam_id
            FROM auth_sessions
            WHERE token_hash = @token_hash AND expires_at > @now;
            """;

        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("now", now);
        object? result = await command.ExecuteScalarAsync(cancellationToken);
        return result as string;
    }

    public async Task StoreGameServerCredentialAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            DELETE FROM game_server_credentials
            WHERE dungeon_session_id = @dungeon_session_id
               OR expires_at <= CURRENT_TIMESTAMP;

            INSERT INTO game_server_credentials (
                token_hash, server_id, dungeon_session_id, expires_at)
            VALUES (
                @token_hash, @server_id, @dungeon_session_id, @expires_at);
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("server_id", serverId);
        command.Parameters.AddWithValue("dungeon_session_id", dungeonSessionId);
        command.Parameters.AddWithValue("expires_at", expiresAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    public async Task<GameServerCredential?> ResolveGameServerCredentialAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT credential.server_id, credential.dungeon_session_id
            FROM game_server_credentials AS credential
            JOIN dungeon_sessions AS session
              ON session.dungeon_session_id = credential.dungeon_session_id
             AND session.server_id = credential.server_id
            WHERE credential.token_hash = @token_hash
              AND credential.expires_at > @now
              AND session.expires_at > @now
              AND session.state IN ('Loading', 'InProgress');
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("now", now);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        return await reader.ReadAsync(cancellationToken)
            ? new GameServerCredential(reader.GetString(0), reader.GetGuid(1))
            : null;
    }

    public async Task<IReadOnlyList<GameCharacter>> GetCharactersAsync(
        string steamId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT character_id, roster_id, steam_id, name, created_at
            FROM characters
            WHERE steam_id = @steam_id
            ORDER BY created_at;
            """;

        List<GameCharacter> characters = [];
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("steam_id", steamId);
        await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            characters.Add(new GameCharacter(
                reader.GetGuid(0),
                reader.GetGuid(1),
                reader.GetString(2),
                reader.GetString(3),
                reader.GetFieldValue<DateTimeOffset>(4)));
        }

        return characters;
    }

    public async Task<IReadOnlyList<Roster>> GetRostersAsync(
        string steamId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT roster_id, steam_id, world_id, created_at
            FROM rosters
            WHERE steam_id = @steam_id
            ORDER BY world_id;
            """;

        List<Roster> rosters = [];
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("steam_id", steamId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            rosters.Add(new Roster(
                reader.GetGuid(0),
                reader.GetString(1),
                reader.GetString(2),
                reader.GetFieldValue<DateTimeOffset>(3)));
        }

        return rosters;
    }

    public async Task<GameCharacter> CreateCharacterAsync(
        string steamId,
        string name,
        DateTimeOffset createdAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            INSERT INTO characters (
                character_id, roster_id, steam_id, name, created_at, updated_at)
            SELECT
                @character_id, roster_id, @steam_id, @name,
                @created_at, @created_at
            FROM rosters
            WHERE steam_id = @steam_id AND world_id = 'main'
            RETURNING roster_id;
            """;

        Guid characterId = Guid.NewGuid();
        try
        {
            await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
            await using NpgsqlCommand command = new(sql, connection);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            command.Parameters.AddWithValue("name", name);
            command.Parameters.AddWithValue("created_at", createdAt);
            object? result = await command.ExecuteScalarAsync(cancellationToken);
            if (result is not Guid rosterId)
            {
                throw new InvalidOperationException(
                    "The account does not have a default roster.");
            }

            return new GameCharacter(
                characterId,
                rosterId,
                steamId,
                name,
                createdAt);
        }
        catch (PostgresException exception)
            when (exception.SqlState == PostgresErrorCodes.UniqueViolation)
        {
            throw new DuplicateCharacterNameException(name);
        }
    }

    public async Task<string?> GetCharacterOwnerAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        const string sql = "SELECT steam_id FROM characters WHERE character_id = @character_id;";
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("character_id", characterId);
        object? result = await command.ExecuteScalarAsync(cancellationToken);
        return result as string;
    }

    public async Task<CharacterEconomyContext?> GetCharacterEconomyContextAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT character_id, roster_id, steam_id
            FROM characters
            WHERE character_id = @character_id;
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("character_id", characterId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }

        return new CharacterEconomyContext(
            reader.GetGuid(0),
            reader.GetGuid(1),
            reader.GetString(2));
    }

    public async Task<DungeonSession?> GetDungeonSessionAsync(
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);
        DungeonSession? session = await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public async Task<DungeonSession?> GetActiveDungeonSessionForCharacterAsync(
        Guid characterId,
        string steamId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);

        Guid? dungeonSessionId = null;
        await using (NpgsqlCommand command = new(
            """
            SELECT s.dungeon_session_id
            FROM dungeon_sessions s
            JOIN dungeon_session_members m
              ON m.dungeon_session_id = s.dungeon_session_id
            JOIN character_session_leases l
              ON l.character_id = m.character_id
             AND l.dungeon_session_id = s.dungeon_session_id
            WHERE m.character_id = @character_id
              AND m.steam_id = @steam_id
              AND s.state IN ('Waiting', 'Loading', 'InProgress')
              AND s.expires_at > @now
              AND m.lease_expires_at > @now
              AND l.expires_at > @now
            ORDER BY s.updated_at DESC, s.dungeon_session_id
            LIMIT 1;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            command.Parameters.AddWithValue("now", now);
            object? result = await command.ExecuteScalarAsync(cancellationToken);
            dungeonSessionId = result is Guid value ? value : null;
        }

        DungeonSession? session = dungeonSessionId.HasValue
            ? await ReadDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId.Value,
                cancellationToken)
            : null;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public async Task<DungeonSession> CreateDungeonSessionAsync(
        string steamId,
        Guid characterId,
        string dungeonId,
        string difficulty,
        DateTimeOffset now,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        Guid dungeonSessionId = Guid.NewGuid();
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);

        const string sessionSql = """
            INSERT INTO dungeon_sessions (
                dungeon_session_id, dungeon_id, difficulty, state,
                created_at, updated_at, expires_at)
            VALUES (
                @dungeon_session_id, @dungeon_id, @difficulty, 'Waiting',
                @now, @now, @expires_at);
            """;
        await using (NpgsqlCommand command = new(
            sessionSql,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("dungeon_id", dungeonId);
            command.Parameters.AddWithValue("difficulty", difficulty);
            command.Parameters.AddWithValue("now", now);
            command.Parameters.AddWithValue("expires_at", expiresAt);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }

        try
        {
            await InsertCharacterLeaseAsync(
                connection,
                transaction,
                characterId,
                dungeonSessionId,
                expiresAt,
                now,
                cancellationToken);
        }
        catch (PostgresException exception)
            when (exception.SqlState == PostgresErrorCodes.UniqueViolation)
        {
            throw new CharacterSessionConflictException(characterId);
        }

        const string memberSql = """
            INSERT INTO dungeon_session_members (
                dungeon_session_id, character_id, steam_id,
                joined_at, lease_expires_at)
            VALUES (
                @dungeon_session_id, @character_id, @steam_id,
                @now, @expires_at);
            """;
        await using (NpgsqlCommand command = new(
            memberSql,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            command.Parameters.AddWithValue("now", now);
            command.Parameters.AddWithValue("expires_at", expiresAt);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }

        DungeonSession session = (await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public async Task<DungeonSession> JoinDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        int maxPartySize,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);
        (
            DungeonSessionState State,
            string? ServerId,
            string? ServerAddress)? locked =
            await LockDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId,
                cancellationToken);
        if (locked is null)
        {
            throw new DungeonSessionNotFoundException(dungeonSessionId);
        }

        if (locked.Value.State != DungeonSessionState.Waiting)
        {
            throw new DungeonSessionNotJoinableException(dungeonSessionId);
        }

        const string existingMemberSql = """
            SELECT EXISTS (
                SELECT 1
                FROM dungeon_session_members
                WHERE dungeon_session_id = @dungeon_session_id
                  AND character_id = @character_id
                  AND steam_id = @steam_id);
            """;
        await using (NpgsqlCommand command = new(
            existingMemberSql,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            bool alreadyMember = (bool)(await command.ExecuteScalarAsync(
                cancellationToken))!;
            if (alreadyMember)
            {
                DungeonSession existingSession =
                    (await ReadDungeonSessionAsync(
                        connection,
                        transaction,
                        dungeonSessionId,
                        cancellationToken))!;
                await transaction.CommitAsync(cancellationToken);
                return existingSession;
            }
        }

        await using (NpgsqlCommand command = new(
            """
            SELECT COUNT(*)
            FROM dungeon_session_members
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            long memberCount = (long)(await command.ExecuteScalarAsync(
                cancellationToken))!;
            if (memberCount >= maxPartySize)
            {
                throw new DungeonSessionFullException(dungeonSessionId);
            }
        }

        try
        {
            await InsertCharacterLeaseAsync(
                connection,
                transaction,
                characterId,
                dungeonSessionId,
                leaseExpiresAt,
                now,
                cancellationToken);
        }
        catch (PostgresException exception)
            when (exception.SqlState == PostgresErrorCodes.UniqueViolation)
        {
            throw new CharacterSessionConflictException(characterId);
        }

        await using (NpgsqlCommand command = new(
            """
            INSERT INTO dungeon_session_members (
                dungeon_session_id, character_id, steam_id,
                joined_at, lease_expires_at)
            VALUES (
                @dungeon_session_id, @character_id, @steam_id,
                @now, @lease_expires_at);

            UPDATE dungeon_sessions
            SET updated_at = @now,
                expires_at = GREATEST(expires_at, @lease_expires_at)
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            command.Parameters.AddWithValue("now", now);
            command.Parameters.AddWithValue(
                "lease_expires_at",
                leaseExpiresAt);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }

        await ExtendDungeonLeaseAsync(
            connection,
            transaction,
            dungeonSessionId,
            now,
            leaseExpiresAt,
            cancellationToken);
        DungeonSession session = (await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public async Task<DungeonSession> LeaveDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);
        (
            DungeonSessionState State,
            string? ServerId,
            string? ServerAddress)? locked =
            await LockDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId,
                cancellationToken);
        if (locked is null)
        {
            throw new DungeonSessionNotFoundException(dungeonSessionId);
        }

        if (locked.Value.State != DungeonSessionState.Waiting)
        {
            throw new DungeonSessionNotJoinableException(dungeonSessionId);
        }

        int removed;
        await using (NpgsqlCommand command = new(
            """
            DELETE FROM dungeon_session_members
            WHERE dungeon_session_id = @dungeon_session_id
              AND character_id = @character_id
              AND steam_id = @steam_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            removed = await command.ExecuteNonQueryAsync(cancellationToken);
        }

        if (removed == 0)
        {
            throw new DungeonSessionMembershipException(dungeonSessionId);
        }

        await using (NpgsqlCommand command = new(
            """
            DELETE FROM character_session_leases
            WHERE character_id = @character_id
              AND dungeon_session_id = @dungeon_session_id;

            UPDATE dungeon_sessions
            SET state = CASE
                    WHEN NOT EXISTS (
                        SELECT 1 FROM dungeon_session_members
                        WHERE dungeon_session_id = @dungeon_session_id)
                    THEN 'Closed'
                    ELSE state
                END,
                updated_at = @now,
                expires_at = CASE
                    WHEN NOT EXISTS (
                        SELECT 1 FROM dungeon_session_members
                        WHERE dungeon_session_id = @dungeon_session_id)
                    THEN @now
                    ELSE expires_at
                END
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("now", now);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }

        DungeonSession session = (await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public Task<DungeonSession> AssignDungeonServerAsync(
        Guid dungeonSessionId,
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken) =>
        TransitionDungeonSessionAsync(
            dungeonSessionId,
            serverId,
            DungeonSessionState.Waiting,
            DungeonSessionState.Loading,
            serverAddress,
            now,
            leaseExpiresAt,
            cancellationToken);

    public async Task<DungeonSession?> ClaimNextDungeonSessionAsync(
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);

        Guid? dungeonSessionId;
        await using (NpgsqlCommand claimCommand = new(
            """
            SELECT s.dungeon_session_id
            FROM dungeon_sessions s
            WHERE s.state = 'Waiting'
              AND s.expires_at > @now
              AND EXISTS (
                  SELECT 1
                  FROM dungeon_session_members m
                  WHERE m.dungeon_session_id = s.dungeon_session_id)
            ORDER BY s.created_at, s.dungeon_session_id
            FOR UPDATE OF s SKIP LOCKED
            LIMIT 1;
            """,
            connection,
            transaction))
        {
            claimCommand.Parameters.AddWithValue("now", now);
            object? claimed = await claimCommand.ExecuteScalarAsync(
                cancellationToken);
            dungeonSessionId = claimed is Guid value ? value : null;
        }

        if (dungeonSessionId is null)
        {
            await transaction.CommitAsync(cancellationToken);
            return null;
        }

        await using (NpgsqlCommand assignCommand = new(
            """
            UPDATE dungeon_sessions
            SET state = 'Loading',
                server_id = @server_id,
                server_address = @server_address
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            assignCommand.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId.Value);
            assignCommand.Parameters.AddWithValue("server_id", serverId);
            assignCommand.Parameters.AddWithValue(
                "server_address",
                serverAddress);
            await assignCommand.ExecuteNonQueryAsync(cancellationToken);
        }

        await ExtendDungeonLeaseAsync(
            connection,
            transaction,
            dungeonSessionId.Value,
            now,
            leaseExpiresAt,
            cancellationToken);
        DungeonSession session = (await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId.Value,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public Task<DungeonSession> StartDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken) =>
        TransitionDungeonSessionAsync(
            dungeonSessionId,
            serverId,
            DungeonSessionState.Loading,
            DungeonSessionState.InProgress,
            null,
            now,
            leaseExpiresAt,
            cancellationToken);

    public async Task<DungeonSession> HeartbeatDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);
        (
            DungeonSessionState State,
            string? ServerId,
            string? ServerAddress)? locked =
            await LockDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId,
                cancellationToken);
        if (locked is null)
        {
            throw new DungeonSessionNotFoundException(dungeonSessionId);
        }

        EnsureServerMatches(dungeonSessionId, locked.Value.ServerId, serverId);
        if (locked.Value.State is not (
            DungeonSessionState.Loading
            or DungeonSessionState.InProgress))
        {
            throw new DungeonSessionStateConflictException(
                dungeonSessionId,
                locked.Value.State);
        }

        await ExtendDungeonLeaseAsync(
            connection,
            transaction,
            dungeonSessionId,
            now,
            leaseExpiresAt,
            cancellationToken);
        DungeonSession session = (await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public async Task<DungeonSession> FinishDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DungeonSessionState outcome,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);
        (
            DungeonSessionState State,
            string? ServerId,
            string? ServerAddress)? locked =
            await LockDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId,
                cancellationToken);
        if (locked is null)
        {
            throw new DungeonSessionNotFoundException(dungeonSessionId);
        }

        EnsureServerMatches(dungeonSessionId, locked.Value.ServerId, serverId);
        if (outcome is not (
                DungeonSessionState.Cleared
                or DungeonSessionState.Failed))
        {
            throw new DungeonSessionStateConflictException(
                dungeonSessionId,
                locked.Value.State);
        }

        if (locked.Value.State == outcome)
        {
            DungeonSession completedSession = (await ReadDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId,
                cancellationToken))!;
            await transaction.CommitAsync(cancellationToken);
            return completedSession;
        }

        if (locked.Value.State is not (
                DungeonSessionState.Loading
                or DungeonSessionState.InProgress))
        {
            throw new DungeonSessionStateConflictException(
                dungeonSessionId,
                locked.Value.State);
        }

        await using (NpgsqlCommand command = new(
            """
            UPDATE dungeon_sessions
            SET state = @outcome,
                updated_at = @now,
                expires_at = @now
            WHERE dungeon_session_id = @dungeon_session_id;

            UPDATE dungeon_session_members
            SET lease_expires_at = @now
            WHERE dungeon_session_id = @dungeon_session_id;

            DELETE FROM character_session_leases
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("outcome", outcome.ToString());
            command.Parameters.AddWithValue("now", now);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }

        DungeonSession session = (await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public async Task<DungeonRewardSettlement> EnqueueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string serverId,
        string rewardVersion,
        string commandFingerprint,
        IReadOnlyList<CurrencyChange> changes,
        IReadOnlyList<DungeonItemReward> itemRewards,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);

        (
            DungeonSessionState State,
            string? ServerId,
            string? ServerAddress)? locked =
            await LockDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId,
                cancellationToken);
        if (locked is null)
        {
            throw new DungeonSessionNotFoundException(dungeonSessionId);
        }

        EnsureServerMatches(dungeonSessionId, locked.Value.ServerId, serverId);
        DungeonRewardSettlement? existing = await ReadSettlementAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken);
        if (existing is not null)
        {
            if (!string.Equals(
                    existing.RewardVersion,
                    rewardVersion,
                    StringComparison.Ordinal)
                || !string.Equals(
                    existing.CommandFingerprint,
                    commandFingerprint,
                    StringComparison.Ordinal))
            {
                throw new DungeonRewardSettlementConflictException(
                    dungeonSessionId);
            }

            await transaction.CommitAsync(cancellationToken);
            return existing;
        }

        if (locked.Value.State != DungeonSessionState.InProgress)
        {
            throw new DungeonSessionStateConflictException(
                dungeonSessionId,
                locked.Value.State);
        }

        List<Guid> characterIds = [];
        await using (NpgsqlCommand readMembers = new(
            """
            SELECT character_id
            FROM dungeon_session_members
            WHERE dungeon_session_id = @dungeon_session_id
            ORDER BY character_id;
            """,
            connection,
            transaction))
        {
            readMembers.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            await using NpgsqlDataReader reader =
                await readMembers.ExecuteReaderAsync(cancellationToken);
            while (await reader.ReadAsync(cancellationToken))
            {
                characterIds.Add(reader.GetGuid(0));
            }
        }

        if (characterIds.Count == 0)
        {
            throw new DungeonSessionMembershipException(dungeonSessionId);
        }

        await using (NpgsqlCommand command = new(
            """
            INSERT INTO dungeon_reward_settlements (
                dungeon_session_id, server_id, reward_version,
                command_fingerprint, state, character_ids, currency_changes,
                item_rewards, attempt_count, next_attempt_at, created_at,
                updated_at)
            VALUES (
                @dungeon_session_id, @server_id, @reward_version,
                @command_fingerprint, 'Pending', @character_ids,
                @currency_changes, @item_rewards, 0, @now, @now, @now);

            UPDATE dungeon_sessions
            SET state = 'SettlementPending',
                updated_at = @now,
                expires_at = @now
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            command.Parameters.AddWithValue("server_id", serverId);
            command.Parameters.AddWithValue("reward_version", rewardVersion);
            command.Parameters.AddWithValue(
                "command_fingerprint",
                commandFingerprint);
            command.Parameters.AddWithValue(
                "character_ids",
                NpgsqlDbType.Jsonb,
                JsonSerializer.Serialize(
                    characterIds,
                    SettlementJsonOptions));
            command.Parameters.AddWithValue(
                "currency_changes",
                NpgsqlDbType.Jsonb,
                JsonSerializer.Serialize(changes, SettlementJsonOptions));
            command.Parameters.AddWithValue(
                "item_rewards",
                NpgsqlDbType.Jsonb,
                JsonSerializer.Serialize(itemRewards, SettlementJsonOptions));
            command.Parameters.AddWithValue("now", now);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }

        DungeonRewardSettlement settlement = (await ReadSettlementAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return settlement;
    }

    public async Task<DungeonRewardSettlement?> ClaimNextDungeonRewardSettlementAsync(
        string workerId,
        DateTimeOffset now,
        DateTimeOffset processingExpiresAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            WITH candidate AS (
                SELECT dungeon_session_id
                FROM dungeon_reward_settlements
                WHERE (state = 'Pending' AND next_attempt_at <= @now)
                   OR (state = 'Processing'
                       AND processing_expires_at <= @now)
                ORDER BY next_attempt_at, created_at, dungeon_session_id
                FOR UPDATE SKIP LOCKED
                LIMIT 1
            )
            UPDATE dungeon_reward_settlements AS settlement
            SET state = 'Processing',
                attempt_count = settlement.attempt_count + 1,
                worker_id = @worker_id,
                processing_expires_at = @processing_expires_at,
                updated_at = @now
            FROM candidate
            WHERE settlement.dungeon_session_id = candidate.dungeon_session_id
            RETURNING settlement.dungeon_session_id;
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        Guid? dungeonSessionId;
        await using (NpgsqlCommand command = new(sql, connection, transaction))
        {
            command.Parameters.AddWithValue("worker_id", workerId);
            command.Parameters.AddWithValue("now", now);
            command.Parameters.AddWithValue(
                "processing_expires_at",
                processingExpiresAt);
            object? result = await command.ExecuteScalarAsync(cancellationToken);
            dungeonSessionId = result is Guid value ? value : null;
        }

        DungeonRewardSettlement? settlement = dungeonSessionId.HasValue
            ? await ReadSettlementAsync(
                connection,
                transaction,
                dungeonSessionId.Value,
                cancellationToken)
            : null;
        await transaction.CommitAsync(cancellationToken);
        return settlement;
    }

    public Task CompleteDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        DateTimeOffset now,
        CancellationToken cancellationToken) =>
        FinalizeDungeonRewardSettlementAsync(
            dungeonSessionId,
            workerId,
            DungeonRewardSettlementState.Completed,
            DungeonSessionState.Cleared,
            null,
            now,
            cancellationToken);

    public Task FailDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        CancellationToken cancellationToken) =>
        FinalizeDungeonRewardSettlementAsync(
            dungeonSessionId,
            workerId,
            DungeonRewardSettlementState.Failed,
            DungeonSessionState.Failed,
            error,
            now,
            cancellationToken);

    public async Task RequeueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        DateTimeOffset nextAttemptAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            UPDATE dungeon_reward_settlements
            SET state = 'Pending',
                next_attempt_at = @next_attempt_at,
                worker_id = NULL,
                processing_expires_at = NULL,
                last_error = @last_error,
                updated_at = @now
            WHERE dungeon_session_id = @dungeon_session_id
              AND state = 'Processing'
              AND worker_id = @worker_id;
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("dungeon_session_id", dungeonSessionId);
        command.Parameters.AddWithValue("worker_id", workerId);
        command.Parameters.AddWithValue("last_error", TruncateError(error));
        command.Parameters.AddWithValue("now", now);
        command.Parameters.AddWithValue("next_attempt_at", nextAttemptAt);
        int affected = await command.ExecuteNonQueryAsync(cancellationToken);
        if (affected != 1)
        {
            throw new InvalidOperationException(
                $"Dungeon reward settlement '{dungeonSessionId}' is no longer owned by worker '{workerId}'.");
        }
    }

    public async Task<bool> IsActiveDungeonSessionMemberAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT EXISTS (
                SELECT 1
                FROM dungeon_sessions s
                JOIN dungeon_session_members m
                  ON m.dungeon_session_id = s.dungeon_session_id
                JOIN character_session_leases l
                  ON l.character_id = m.character_id
                 AND l.dungeon_session_id = s.dungeon_session_id
                WHERE s.dungeon_session_id = @dungeon_session_id
                  AND s.state IN ('Loading', 'InProgress')
                  AND s.server_id IS NOT NULL
                  AND s.expires_at > @now
                  AND m.character_id = @character_id
                  AND m.steam_id = @steam_id
                  AND m.lease_expires_at > @now
                  AND l.expires_at > @now);
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue("now", now);
        return (bool)(await command.ExecuteScalarAsync(cancellationToken))!;
    }

    public async Task<bool> IsAuthorizedGameServerSessionMemberAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT EXISTS (
                SELECT 1
                FROM dungeon_sessions s
                JOIN dungeon_session_members m
                  ON m.dungeon_session_id = s.dungeon_session_id
                JOIN character_session_leases l
                  ON l.character_id = m.character_id
                 AND l.dungeon_session_id = s.dungeon_session_id
                WHERE s.dungeon_session_id = @dungeon_session_id
                  AND s.state IN ('Loading', 'InProgress')
                  AND s.server_id = @server_id
                  AND s.expires_at > @now
                  AND m.character_id = @character_id
                  AND m.steam_id = @steam_id
                  AND m.lease_expires_at > @now
                  AND l.expires_at > @now);
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("server_id", serverId);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue("now", now);
        return (bool)(await command.ExecuteScalarAsync(cancellationToken))!;
    }

    public async Task StoreJoinTicketAsync(
        string tokenHash,
        string steamId,
        Guid characterId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            DELETE FROM game_join_tickets
            WHERE expires_at <= CURRENT_TIMESTAMP;

            INSERT INTO game_join_tickets (
                token_hash, steam_id, character_id,
                dungeon_session_id, expires_at)
            VALUES (
                @token_hash, @steam_id, @character_id,
                @dungeon_session_id, @expires_at);
            """;

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("expires_at", expiresAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    public async Task<ConsumedJoinTicket?> ConsumeJoinTicketAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        // DELETE ... USING ... RETURNING makes validation and consumption atomic.
        const string sql = """
            DELETE FROM game_join_tickets t
            USING dungeon_sessions s,
                  dungeon_session_members m,
                  character_session_leases l
            WHERE t.token_hash = @token_hash
              AND t.expires_at > @now
              AND t.dungeon_session_id = @dungeon_session_id
              AND s.dungeon_session_id = t.dungeon_session_id
              AND s.server_id = @server_id
              AND s.state IN ('Loading', 'InProgress')
              AND s.expires_at > @now
              AND m.dungeon_session_id = s.dungeon_session_id
              AND m.character_id = t.character_id
              AND m.steam_id = t.steam_id
              AND m.lease_expires_at > @now
              AND l.character_id = t.character_id
              AND l.dungeon_session_id = s.dungeon_session_id
              AND l.expires_at > @now
            RETURNING t.dungeon_session_id, t.character_id, t.steam_id;
            """;

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("server_id", serverId);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("now", now);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }

        return new ConsumedJoinTicket(
            reader.GetGuid(0),
            reader.GetGuid(1),
            reader.GetString(2));
    }

    public async Task<IReadOnlyList<InventoryItem>> LoadInventoryAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT item_id, quantity, slot_index, category, instance_id
            FROM inventory_items
            WHERE character_id = @character_id
            ORDER BY slot_index;
            """;

        List<InventoryItem> inventory = [];
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("character_id", characterId);
        await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            inventory.Add(new InventoryItem(
                reader.GetString(0),
                reader.GetInt32(1),
                reader.GetInt32(2),
                reader.GetString(3),
                reader.GetString(4)));
        }

        return inventory;
    }

    public async Task SaveInventoryAsync(
        Guid characterId,
        IReadOnlyList<InventoryItem> inventory,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);

        await using (NpgsqlCommand deleteCommand = new(
            "DELETE FROM inventory_items WHERE character_id = @character_id;",
            connection,
            transaction))
        {
            deleteCommand.Parameters.AddWithValue("character_id", characterId);
            await deleteCommand.ExecuteNonQueryAsync(cancellationToken);
        }

        const string insertSql = """
            INSERT INTO inventory_items (
                character_id, slot_index, item_id, quantity,
                category, instance_id, updated_at)
            VALUES (
                @character_id, @slot_index, @item_id, @quantity,
                @category, @instance_id, @updated_at);
            """;

        foreach (InventoryItem item in inventory)
        {
            await using NpgsqlCommand insertCommand = new(insertSql, connection, transaction);
            insertCommand.Parameters.AddWithValue("character_id", characterId);
            insertCommand.Parameters.AddWithValue("slot_index", item.SlotIndex);
            insertCommand.Parameters.AddWithValue("item_id", item.ItemId);
            insertCommand.Parameters.AddWithValue("quantity", item.Quantity);
            insertCommand.Parameters.AddWithValue("category", item.Category);
            insertCommand.Parameters.AddWithValue("instance_id", item.InstanceId);
            insertCommand.Parameters.AddWithValue("updated_at", updatedAt);
            await insertCommand.ExecuteNonQueryAsync(cancellationToken);
        }

        await using (NpgsqlCommand updateCommand = new(
            "UPDATE characters SET updated_at = @updated_at WHERE character_id = @character_id;",
            connection,
            transaction))
        {
            updateCommand.Parameters.AddWithValue("updated_at", updatedAt);
            updateCommand.Parameters.AddWithValue("character_id", characterId);
            await updateCommand.ExecuteNonQueryAsync(cancellationToken);
        }

        await transaction.CommitAsync(cancellationToken);
    }

    private async Task<DungeonSession> TransitionDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DungeonSessionState expectedState,
        DungeonSessionState targetState,
        string? serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);
        (
            DungeonSessionState State,
            string? ServerId,
            string? ServerAddress)? locked =
            await LockDungeonSessionAsync(
                connection,
                transaction,
                dungeonSessionId,
                cancellationToken);
        if (locked is null)
        {
            throw new DungeonSessionNotFoundException(dungeonSessionId);
        }

        if (locked.Value.State == targetState)
        {
            EnsureServerMatches(
                dungeonSessionId,
                locked.Value.ServerId,
                serverId);
            if (serverAddress is not null
                && !string.Equals(
                    locked.Value.ServerAddress,
                    serverAddress,
                    StringComparison.Ordinal))
            {
                throw new DungeonSessionServerMismatchException(
                    dungeonSessionId);
            }
        }
        else
        {
            if (locked.Value.State != expectedState)
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    locked.Value.State);
            }

            if (expectedState != DungeonSessionState.Waiting)
            {
                EnsureServerMatches(
                    dungeonSessionId,
                    locked.Value.ServerId,
                    serverId);
            }

            await using NpgsqlCommand transitionCommand = new(
                """
                UPDATE dungeon_sessions
                SET state = @target_state,
                    server_id = @server_id,
                    server_address = COALESCE(
                        @server_address,
                        server_address)
                WHERE dungeon_session_id = @dungeon_session_id;
                """,
                connection,
                transaction);
            transitionCommand.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            transitionCommand.Parameters.AddWithValue(
                "target_state",
                targetState.ToString());
            transitionCommand.Parameters.AddWithValue("server_id", serverId);
            NpgsqlParameter serverAddressParameter =
                transitionCommand.Parameters.Add(
                    "server_address",
                    NpgsqlDbType.Varchar);
            serverAddressParameter.Value =
                serverAddress is null ? DBNull.Value : serverAddress;
            await transitionCommand.ExecuteNonQueryAsync(cancellationToken);
        }

        await ExtendDungeonLeaseAsync(
            connection,
            transaction,
            dungeonSessionId,
            now,
            leaseExpiresAt,
            cancellationToken);
        DungeonSession session = (await ReadDungeonSessionAsync(
            connection,
            transaction,
            dungeonSessionId,
            cancellationToken))!;
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    private async Task FinalizeDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        DungeonRewardSettlementState settlementState,
        DungeonSessionState sessionState,
        string? error,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);

        int settlementUpdated;
        await using (NpgsqlCommand updateSettlement = new(
            """
            UPDATE dungeon_reward_settlements
            SET state = @settlement_state,
                worker_id = NULL,
                processing_expires_at = NULL,
                last_error = @last_error,
                updated_at = @now,
                completed_at = @now
            WHERE dungeon_session_id = @dungeon_session_id
              AND state = 'Processing'
              AND worker_id = @worker_id;
            """,
            connection,
            transaction))
        {
            updateSettlement.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            updateSettlement.Parameters.AddWithValue("worker_id", workerId);
            updateSettlement.Parameters.AddWithValue(
                "settlement_state",
                settlementState.ToString());
            NpgsqlParameter lastError = updateSettlement.Parameters.Add(
                "last_error",
                NpgsqlDbType.Varchar);
            lastError.Value = error is null
                ? DBNull.Value
                : TruncateError(error);
            updateSettlement.Parameters.AddWithValue("now", now);
            settlementUpdated = await updateSettlement.ExecuteNonQueryAsync(
                cancellationToken);
        }

        if (settlementUpdated != 1)
        {
            throw new InvalidOperationException(
                $"Dungeon reward settlement '{dungeonSessionId}' is no longer owned by worker '{workerId}'.");
        }

        await using (NpgsqlCommand finalizeSession = new(
            """
            UPDATE dungeon_sessions
            SET state = @session_state,
                updated_at = @now,
                expires_at = @now
            WHERE dungeon_session_id = @dungeon_session_id
              AND state = 'SettlementPending';

            UPDATE dungeon_session_members
            SET lease_expires_at = @now
            WHERE dungeon_session_id = @dungeon_session_id;

            DELETE FROM character_session_leases
            WHERE dungeon_session_id = @dungeon_session_id;

            DELETE FROM game_server_credentials
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            finalizeSession.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            finalizeSession.Parameters.AddWithValue(
                "session_state",
                sessionState.ToString());
            finalizeSession.Parameters.AddWithValue("now", now);
            await finalizeSession.ExecuteNonQueryAsync(cancellationToken);
        }

        await transaction.CommitAsync(cancellationToken);
    }

    private static async Task<DungeonRewardSettlement?> ReadSettlementAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid dungeonSessionId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT server_id, reward_version, command_fingerprint, state,
                   character_ids::TEXT, currency_changes::TEXT,
                   item_rewards::TEXT, attempt_count, next_attempt_at, worker_id,
                   processing_expires_at, last_error, created_at,
                   updated_at, completed_at
            FROM dungeon_reward_settlements
            WHERE dungeon_session_id = @dungeon_session_id;
            """;
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("dungeon_session_id", dungeonSessionId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }

        Guid[] characterIds = JsonSerializer.Deserialize<Guid[]>(
                reader.GetString(4),
                SettlementJsonOptions)
            ?? throw new InvalidDataException(
                "A dungeon reward settlement contains invalid character IDs.");
        CurrencyChange[] changes =
            JsonSerializer.Deserialize<CurrencyChange[]>(
                reader.GetString(5),
                SettlementJsonOptions)
            ?? throw new InvalidDataException(
                "A dungeon reward settlement contains invalid currency changes.");
        DungeonItemReward[] itemRewards =
            JsonSerializer.Deserialize<DungeonItemReward[]>(
                reader.GetString(6),
                SettlementJsonOptions)
            ?? throw new InvalidDataException(
                "A dungeon reward settlement contains invalid item rewards.");
        return new DungeonRewardSettlement(
            dungeonSessionId,
            reader.GetString(0),
            reader.GetString(1),
            reader.GetString(2),
            Enum.Parse<DungeonRewardSettlementState>(reader.GetString(3)),
            characterIds,
            changes,
            itemRewards,
            reader.GetInt32(7),
            reader.GetFieldValue<DateTimeOffset>(8),
            reader.IsDBNull(9) ? null : reader.GetString(9),
            reader.IsDBNull(10)
                ? null
                : reader.GetFieldValue<DateTimeOffset>(10),
            reader.IsDBNull(11) ? null : reader.GetString(11),
            reader.GetFieldValue<DateTimeOffset>(12),
            reader.GetFieldValue<DateTimeOffset>(13),
            reader.IsDBNull(14)
                ? null
                : reader.GetFieldValue<DateTimeOffset>(14));
    }

    private static string TruncateError(string error)
    {
        string normalized = error.Trim();
        return normalized.Length <= 512
            ? normalized
            : normalized[..512];
    }

    private static async Task CleanupExpiredDungeonSessionsAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new(
            """
            UPDATE dungeon_sessions
            SET state = 'Closed',
                updated_at = @now,
                expires_at = @now
            WHERE state IN ('Waiting', 'Loading', 'InProgress')
              AND expires_at <= @now;

            UPDATE dungeon_session_members m
            SET lease_expires_at = @now
            FROM dungeon_sessions s
            WHERE m.dungeon_session_id = s.dungeon_session_id
              AND s.state = 'Closed'
              AND m.lease_expires_at > @now;

            DELETE FROM character_session_leases l
            USING dungeon_sessions s
            WHERE l.dungeon_session_id = s.dungeon_session_id
              AND (l.expires_at <= @now
                   OR s.state IN ('Cleared', 'Failed', 'Closed'));
            """,
            connection,
            transaction);
        command.Parameters.AddWithValue("now", now);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task InsertCharacterLeaseAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid characterId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new(
            """
            INSERT INTO character_session_leases (
                character_id, dungeon_session_id, expires_at, updated_at)
            VALUES (
                @character_id, @dungeon_session_id, @expires_at, @now);
            """,
            connection,
            transaction);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("expires_at", expiresAt);
        command.Parameters.AddWithValue("now", now);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task<(
        DungeonSessionState State,
        string? ServerId,
        string? ServerAddress)?> LockDungeonSessionAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid dungeonSessionId,
        CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new(
            """
            SELECT state, server_id, server_address
            FROM dungeon_sessions
            WHERE dungeon_session_id = @dungeon_session_id
            FOR UPDATE;
            """,
            connection,
            transaction);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }

        DungeonSessionState state = Enum.Parse<DungeonSessionState>(
            reader.GetString(0));
        string? serverId = reader.IsDBNull(1) ? null : reader.GetString(1);
        string? serverAddress =
            reader.IsDBNull(2) ? null : reader.GetString(2);
        return (state, serverId, serverAddress);
    }

    private static void EnsureServerMatches(
        Guid dungeonSessionId,
        string? assignedServerId,
        string requestedServerId)
    {
        if (!string.Equals(
            assignedServerId,
            requestedServerId,
            StringComparison.Ordinal))
        {
            throw new DungeonSessionServerMismatchException(
                dungeonSessionId);
        }
    }

    private static async Task ExtendDungeonLeaseAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid dungeonSessionId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlCommand command = new(
            """
            UPDATE dungeon_sessions
            SET updated_at = @now,
                expires_at = @lease_expires_at
            WHERE dungeon_session_id = @dungeon_session_id;

            UPDATE dungeon_session_members
            SET lease_expires_at = @lease_expires_at
            WHERE dungeon_session_id = @dungeon_session_id;

            UPDATE character_session_leases
            SET expires_at = @lease_expires_at,
                updated_at = @now
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("now", now);
        command.Parameters.AddWithValue(
            "lease_expires_at",
            leaseExpiresAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task<DungeonSession?> ReadDungeonSessionAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid dungeonSessionId,
        CancellationToken cancellationToken)
    {
        string dungeonId;
        string difficulty;
        DungeonSessionState state;
        string? serverId;
        string? serverAddress;
        DateTimeOffset createdAt;
        DateTimeOffset updatedAt;
        DateTimeOffset expiresAt;

        await using (NpgsqlCommand command = new(
            """
            SELECT dungeon_id, difficulty, state, server_id,
                   server_address, created_at, updated_at, expires_at
            FROM dungeon_sessions
            WHERE dungeon_session_id = @dungeon_session_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            await using NpgsqlDataReader reader =
                await command.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken))
            {
                return null;
            }

            dungeonId = reader.GetString(0);
            difficulty = reader.GetString(1);
            state = Enum.Parse<DungeonSessionState>(reader.GetString(2));
            serverId = reader.IsDBNull(3) ? null : reader.GetString(3);
            serverAddress =
                reader.IsDBNull(4) ? null : reader.GetString(4);
            createdAt = reader.GetFieldValue<DateTimeOffset>(5);
            updatedAt = reader.GetFieldValue<DateTimeOffset>(6);
            expiresAt = reader.GetFieldValue<DateTimeOffset>(7);
        }

        List<DungeonSessionMember> members = [];
        await using (NpgsqlCommand command = new(
            """
            SELECT character_id, steam_id, joined_at, lease_expires_at
            FROM dungeon_session_members
            WHERE dungeon_session_id = @dungeon_session_id
            ORDER BY joined_at, character_id;
            """,
            connection,
            transaction))
        {
            command.Parameters.AddWithValue(
                "dungeon_session_id",
                dungeonSessionId);
            await using NpgsqlDataReader reader =
                await command.ExecuteReaderAsync(cancellationToken);
            while (await reader.ReadAsync(cancellationToken))
            {
                members.Add(new DungeonSessionMember(
                    reader.GetGuid(0),
                    reader.GetString(1),
                    reader.GetFieldValue<DateTimeOffset>(2),
                    reader.GetFieldValue<DateTimeOffset>(3)));
            }
        }

        return new DungeonSession(
            dungeonSessionId,
            dungeonId,
            difficulty,
            state,
            serverId,
            serverAddress,
            createdAt,
            updatedAt,
            expiresAt,
            members);
    }
}
