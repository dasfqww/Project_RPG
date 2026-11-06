using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository(
    NpgsqlDataSource dataSource,
    ILogger<PostgresGameRepository> logger,
    TimeProvider? connectionClock = null,
    PostgresItemRepository? equipmentItems = null) : IGameRepository, IDungeonPickupIntentRepository
{
    private static readonly JsonSerializerOptions SettlementJsonOptions =
        new(JsonSerializerDefaults.Web);

    public async Task EnsureCreatedAsync(CancellationToken cancellationToken)
    {
        string schema = await PostgresSchemaMigrations.LoadInitializationSqlAsync(AppContext.BaseDirectory, cancellationToken);
        await PostgresSchemaInitializer.ApplyAsync(dataSource, schema, cancellationToken);
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
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await PostgresAdmissionAccess.EnsureAsync(connection, transaction, identity.SteamId, authenticatedAt, cancellationToken);
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("steam_id", identity.SteamId);
        command.Parameters.AddWithValue("owner_steam_id", identity.OwnerSteamId);
        command.Parameters.AddWithValue("vac_banned", identity.VacBanned);
        command.Parameters.AddWithValue("publisher_banned", identity.PublisherBanned);
        command.Parameters.AddWithValue("authenticated_at", authenticatedAt);
        command.Parameters.AddWithValue("roster_id", Guid.NewGuid());
        await command.ExecuteNonQueryAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);
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
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await PostgresAdmissionAccess.EnsureAsync(connection, transaction, steamId, connectionClock?.GetUtcNow() ?? DateTimeOffset.UtcNow, cancellationToken);
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("expires_at", expiresAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);
    }

    public async Task<string?> ResolveSessionAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT steam_id
            FROM auth_sessions
            WHERE token_hash = @token_hash AND expires_at > @now
              AND NOT EXISTS (SELECT 1 FROM ops_account_restrictions r WHERE r.account_id=auth_sessions.steam_id AND r.state='Active' AND r.expires_at>@now);
            """;

        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("now", now);
        object? result = await command.ExecuteScalarAsync(cancellationToken);
        return result as string;
    }

    public async Task RevokeSessionAsync(string tokenHash, string steamId, CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(
            "DELETE FROM auth_sessions WHERE token_hash = @token_hash AND steam_id = @steam_id;", connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("steam_id", steamId);
        await command.ExecuteNonQueryAsync(cancellationToken);
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
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT credential.server_id,
                   credential.dungeon_session_id,
                   session.state
            FROM game_server_credentials AS credential
            JOIN dungeon_sessions AS session
              ON session.dungeon_session_id = credential.dungeon_session_id
             AND session.server_id = credential.server_id
            WHERE credential.token_hash = @token_hash
              AND credential.expires_at > @now
              AND (
                    (session.state IN ('Loading', 'InProgress')
                     AND session.expires_at > @now)
                 OR (session.state = 'SettlementPending' AND session.updated_at >= @post_session_cutoff)
                 OR (session.state IN ('Cleared', 'Failed')
                     AND session.updated_at >= @post_session_cutoff)
                 OR (session.state = 'Closed' AND session.updated_at >= @post_session_cutoff
                     AND EXISTS (SELECT 1 FROM dungeon_session_abandonments AS receipt
                         WHERE receipt.dungeon_session_id = session.dungeon_session_id
                           AND receipt.session_state_at_abandonment = 'Closed'))
                 OR (session.state = 'Closed' AND session.updated_at >= @post_session_cutoff
                     AND EXISTS (SELECT 1 FROM dungeon_session_disconnection_expulsions AS receipt
                         WHERE receipt.dungeon_session_id = session.dungeon_session_id)));
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("now", now);
        command.Parameters.AddWithValue(
            "post_session_cutoff",
            now - postSessionGrace);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }
        string sessionState = reader.GetString(2);
        return new GameServerCredential(
            reader.GetString(0),
            reader.GetGuid(1),
            IsSecurityTelemetryOnly:
                sessionState is "SettlementPending" or "Cleared" or "Failed" or "Closed");
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
            await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
            await using NpgsqlCommand command = new(sql, connection, transaction);
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

            foreach (ItemRecord starter in FirstSliceStarterEquipment.Create(characterId))
                await PostgresItemRepository.WriteRecordAsync(
                    connection, transaction, starter, true, createdAt, cancellationToken);
            await InitializeProgressionAsync(connection, transaction, characterId, createdAt, cancellationToken);
            await transaction.CommitAsync(cancellationToken);

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

    public async Task<DungeonMembershipSnapshot?> GetDungeonMembershipSnapshotAsync(
        Guid dungeonSessionId,
        string serverId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(serverId)) throw new DungeonSessionServerMismatchException(dungeonSessionId);
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        // The same parent lock serializes abandonment, settlement and admission.
        // Separate statements cannot mix pre-removal members with post-removal receipts.
        var locked = await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken);
        if (locked is null) return null;
        EnsureServerMatches(dungeonSessionId, locked.Value.ServerId, serverId);
        DungeonSession session = (await ReadDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken))!;
        List<DungeonAbandonment> receipts = [];
        await using (NpgsqlCommand command = new("""
            SELECT character_id, steam_id, abandoned_at, session_state_at_abandonment
            FROM dungeon_session_abandonments
            WHERE dungeon_session_id = @session_id
            ORDER BY character_id;
            """, connection, transaction))
        {
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
            while (await reader.ReadAsync(cancellationToken))
                receipts.Add(new(dungeonSessionId, reader.GetGuid(0), reader.GetString(1),
                    reader.GetFieldValue<DateTimeOffset>(2), Enum.Parse<DungeonSessionState>(reader.GetString(3))));
        }
        IReadOnlyList<DungeonDisconnectionExpulsion> expulsions = await ReadDisconnectionExpulsionsAsync(
            connection, transaction, dungeonSessionId, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return new(session.DungeonSessionId, serverId, session.State, session.UpdatedAt, session.ExpiresAt,
            session.Members.OrderBy(member => member.CharacterId).ToArray(), receipts.ToArray(), expulsions,
            connectionClock?.GetUtcNow() ?? DateTimeOffset.UtcNow);
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
        await PostgresAdmissionAccess.EnsureAsync(connection, transaction, steamId, connectionClock?.GetUtcNow() ?? now, cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(
            connection,
            transaction,
            now,
            cancellationToken);

        const string sessionSql = """
            INSERT INTO dungeon_sessions (
                dungeon_session_id, dungeon_id, difficulty, state,
                created_at, updated_at, expires_at, leader_character_id, lobby_revision)
            VALUES (
                @dungeon_session_id, @dungeon_id, @difficulty, 'Waiting',
                @now, @now, @expires_at, @leader_character_id, 1);
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
            command.Parameters.AddWithValue("leader_character_id", characterId);
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
        await PostgresAdmissionAccess.EnsureAsync(connection, transaction, steamId, connectionClock?.GetUtcNow() ?? now, cancellationToken);
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

        DungeonSession beforeJoin = (await ReadDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken))!;
        if (beforeJoin.DepartureRequestedAt.HasValue)
            throw new DungeonSessionNotJoinableException(dungeonSessionId);
        _ = DungeonDeparturePolicy.NextRevision(beforeJoin);

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
                lobby_revision = lobby_revision + 1,
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

        DungeonSession beforeLeave = (await ReadDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken))!;
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
        session = DungeonDeparturePolicy.WithRoster(beforeLeave, session.Members, now) with
        { State = session.State, ExpiresAt = session.ExpiresAt };
        await WriteDungeonLobbyAsync(connection, transaction, session, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return session;
    }

    public async Task<DungeonAbandonment> AbandonDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(connection, transaction, now, cancellationToken);
        var locked = await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken);
        if (locked is null) throw new DungeonSessionNotFoundException(dungeonSessionId);

        DungeonAbandonment? existing = null;
        await using (NpgsqlCommand command = new("""
            SELECT steam_id, abandoned_at, session_state_at_abandonment
            FROM dungeon_session_abandonments
            WHERE dungeon_session_id = @session_id AND character_id = @character_id;
            """, connection, transaction))
        {
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
            if (await reader.ReadAsync(cancellationToken))
                existing = new DungeonAbandonment(dungeonSessionId, characterId, reader.GetString(0),
                    reader.GetFieldValue<DateTimeOffset>(1), Enum.Parse<DungeonSessionState>(reader.GetString(2)));
        }
        if (existing is not null)
        {
            if (!string.Equals(existing.SteamId, steamId, StringComparison.Ordinal))
                throw new DungeonSessionMembershipException(dungeonSessionId);
            await transaction.CommitAsync(cancellationToken);
            return existing;
        }
        if (locked.Value.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress))
            throw new DungeonSessionStateConflictException(dungeonSessionId, locked.Value.State);

        await using (NpgsqlCommand command = new("""
            DELETE FROM dungeon_session_members
            WHERE dungeon_session_id = @session_id
              AND character_id = @character_id AND steam_id = @steam_id;
            """, connection, transaction))
        {
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            if (await command.ExecuteNonQueryAsync(cancellationToken) != 1)
                throw new DungeonSessionMembershipException(dungeonSessionId);
        }

        DungeonSessionState state;
        await using (NpgsqlCommand command = new("""
            DELETE FROM character_session_leases
            WHERE character_id = @character_id AND dungeon_session_id = @session_id;
            DELETE FROM game_join_tickets
            WHERE character_id = @character_id AND dungeon_session_id = @session_id;
            UPDATE dungeon_sessions
            SET state = CASE WHEN NOT EXISTS (
                    SELECT 1 FROM dungeon_session_members WHERE dungeon_session_id = @session_id)
                    THEN 'Closed' ELSE state END,
                expires_at = CASE WHEN NOT EXISTS (
                    SELECT 1 FROM dungeon_session_members WHERE dungeon_session_id = @session_id)
                    THEN @now ELSE expires_at END,
                updated_at = @now
            WHERE dungeon_session_id = @session_id
            RETURNING state;
            """, connection, transaction))
        {
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("now", now);
            state = Enum.Parse<DungeonSessionState>((string)(await command.ExecuteScalarAsync(cancellationToken))!);
        }

        DateTimeOffset storedAt;
        await using (NpgsqlCommand command = new("""
            INSERT INTO dungeon_session_abandonments
                (dungeon_session_id, character_id, steam_id, abandoned_at, session_state_at_abandonment)
            VALUES (@session_id, @character_id, @steam_id, @now, @state)
            RETURNING abandoned_at;
            """, connection, transaction))
        {
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            command.Parameters.AddWithValue("now", now);
            command.Parameters.AddWithValue("state", state.ToString());
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
            await reader.ReadAsync(cancellationToken);
            storedAt = reader.GetFieldValue<DateTimeOffset>(0);
        }
        // Recipient capture and admission use this same parent-row lock. Nothing
        // can observe a committed receipt without the membership/lease removal.
        await transaction.CommitAsync(cancellationToken);
        return new DungeonAbandonment(dungeonSessionId, characterId, steamId, storedAt, state);
    }

    public async Task<DateTimeOffset> RecordDungeonMemberDisconnectionAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        long connectionGeneration,
        DateTimeOffset now,
        CancellationToken cancellationToken,
        DateTimeOffset? observedAtUtc = null)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(connection, transaction, now, cancellationToken);
        var locked = await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken);
        if (locked is null) throw new DungeonSessionNotFoundException(dungeonSessionId);
        now = connectionClock?.GetUtcNow() ?? now;
        EnsureServerMatches(dungeonSessionId, locked.Value.ServerId, serverId);
        if (locked.Value.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress))
            throw new DungeonSessionStateConflictException(dungeonSessionId, locked.Value.State);

        // The parent lock serializes this observation with admission and settlement.
        DungeonSession session = (await ReadDungeonSessionAsync(
            connection, transaction, dungeonSessionId, cancellationToken))!;
        DungeonSessionMember? member = session.Members.SingleOrDefault(value =>
            value.CharacterId == characterId && value.SteamId == steamId);
        if (member is null || connectionGeneration <= 0
            || member.ConnectionGeneration != connectionGeneration)
            throw new DungeonSessionMembershipException(dungeonSessionId);
        DateTimeOffset observation = member.DisconnectedAt
            ?? DungeonConnectionPolicy.ResolveObservation(observedAtUtc, member.JoinedAt, now);

        await using NpgsqlCommand command = new("""
            UPDATE dungeon_session_members
            SET disconnected_at = COALESCE(disconnected_at, @observed_at),
                disconnected_generation = @connection_generation
            WHERE dungeon_session_id = @session_id
              AND character_id = @character_id AND steam_id = @steam_id
              AND connection_generation = @connection_generation
              AND connection_generation > 0
            RETURNING disconnected_at;
            """, connection, transaction);
        command.Parameters.AddWithValue("session_id", dungeonSessionId);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("connection_generation", connectionGeneration);
        command.Parameters.AddWithValue("observed_at", observation);
        DateTimeOffset disconnectedAt;
        await using (NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken))
        {
            if (!await reader.ReadAsync(cancellationToken))
                throw new DungeonSessionMembershipException(dungeonSessionId);
            disconnectedAt = reader.GetFieldValue<DateTimeOffset>(0);
        }
        await transaction.CommitAsync(cancellationToken);
        return disconnectedAt;
    }

    public async Task<IReadOnlyList<DungeonDisconnectionExpulsion>> ExpelDisconnectedDungeonMembersAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(connection, transaction, now, cancellationToken);
        var locked = await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken);
        if (locked is null) throw new DungeonSessionNotFoundException(dungeonSessionId);
        now = connectionClock?.GetUtcNow() ?? now;
        EnsureServerMatches(dungeonSessionId, locked.Value.ServerId, serverId);
        DungeonSession session = (await ReadDungeonSessionAsync(
            connection, transaction, dungeonSessionId, cancellationToken))!;
        if (session.State is DungeonSessionState.Loading or DungeonSessionState.InProgress
            && session.ExpiresAt > now)
        {
                List<(Guid CharacterId, string SteamId, DateTimeOffset DisconnectedAt)> expired = [];
                await using (NpgsqlCommand command = new("""
                    DELETE FROM dungeon_session_members
                    WHERE dungeon_session_id = @session_id
                      AND disconnected_at IS NOT NULL
                      AND @now - disconnected_at >= @grace
                    RETURNING character_id, steam_id, disconnected_at;
                    """, connection, transaction))
                {
                    command.Parameters.AddWithValue("session_id", dungeonSessionId);
                    command.Parameters.AddWithValue("now", now);
                    command.Parameters.AddWithValue("grace", DungeonConnectionPolicy.UnexpectedDisconnectGrace);
                    await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
                    while (await reader.ReadAsync(cancellationToken))
                        expired.Add((reader.GetGuid(0), reader.GetString(1), reader.GetFieldValue<DateTimeOffset>(2)));
                }
                if (expired.Count > 0)
                {
                    Guid[] ids = expired.Select(member => member.CharacterId).ToArray();
                    foreach (var member in expired)
                    {
                        await using NpgsqlCommand receipt = new("""
                            INSERT INTO dungeon_session_disconnection_expulsions
                                (dungeon_session_id, character_id, steam_id, disconnected_at, expired_at, processed_at)
                            VALUES (@session_id, @character_id, @steam_id, @disconnected_at, @expired_at, @processed_at);
                            """, connection, transaction);
                        receipt.Parameters.AddWithValue("session_id", dungeonSessionId);
                        receipt.Parameters.AddWithValue("character_id", member.CharacterId);
                        receipt.Parameters.AddWithValue("steam_id", member.SteamId);
                        receipt.Parameters.AddWithValue("disconnected_at", member.DisconnectedAt);
                        receipt.Parameters.AddWithValue("expired_at", member.DisconnectedAt + DungeonConnectionPolicy.UnexpectedDisconnectGrace);
                        receipt.Parameters.AddWithValue("processed_at", now);
                        await receipt.ExecuteNonQueryAsync(cancellationToken);
                    }
                    await using NpgsqlCommand cleanup = new("""
                        DELETE FROM character_session_leases
                        WHERE dungeon_session_id = @session_id AND character_id = ANY(@character_ids);
                        DELETE FROM game_join_tickets
                        WHERE dungeon_session_id = @session_id AND character_id = ANY(@character_ids);
                        UPDATE dungeon_sessions
                        SET state = CASE WHEN NOT EXISTS (
                                SELECT 1 FROM dungeon_session_members WHERE dungeon_session_id = @session_id)
                                THEN 'Closed' ELSE state END,
                            expires_at = CASE WHEN NOT EXISTS (
                                SELECT 1 FROM dungeon_session_members WHERE dungeon_session_id = @session_id)
                                THEN @now ELSE expires_at END,
                            updated_at = @now
                        WHERE dungeon_session_id = @session_id;
                        """, connection, transaction);
                    cleanup.Parameters.AddWithValue("session_id", dungeonSessionId);
                    cleanup.Parameters.AddWithValue("character_ids", ids);
                    cleanup.Parameters.AddWithValue("now", now);
                    await cleanup.ExecuteNonQueryAsync(cancellationToken);
                }
        }
        IReadOnlyList<DungeonDisconnectionExpulsion> receipts = await ReadDisconnectionExpulsionsAsync(
            connection, transaction, dungeonSessionId, cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return receipts;
    }

    private static async Task<IReadOnlyList<DungeonDisconnectionExpulsion>> ReadDisconnectionExpulsionsAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid dungeonSessionId,
        CancellationToken cancellationToken)
    {
        List<DungeonDisconnectionExpulsion> receipts = [];
        await using NpgsqlCommand command = new("""
            SELECT character_id, steam_id, disconnected_at, expired_at, processed_at
            FROM dungeon_session_disconnection_expulsions
            WHERE dungeon_session_id = @session_id
            ORDER BY character_id;
            """, connection, transaction);
        command.Parameters.AddWithValue("session_id", dungeonSessionId);
        await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
            receipts.Add(new(dungeonSessionId, reader.GetGuid(0), reader.GetString(1),
                reader.GetFieldValue<DateTimeOffset>(2), reader.GetFieldValue<DateTimeOffset>(3),
                reader.GetFieldValue<DateTimeOffset>(4)));
        return receipts;
    }

    public async Task<bool> ConfirmDungeonMemberAdmissionAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        long connectionGeneration,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await PostgresAdmissionAccess.EnsureAsync(connection, transaction, steamId, connectionClock?.GetUtcNow() ?? now, cancellationToken);
        await CleanupExpiredDungeonSessionsAsync(connection, transaction, now, cancellationToken);
        var locked = await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken);
        if (locked is null) throw new DungeonSessionNotFoundException(dungeonSessionId);
        now = connectionClock?.GetUtcNow() ?? now;
        EnsureServerMatches(dungeonSessionId, locked.Value.ServerId, serverId);
        if (locked.Value.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress))
            throw new DungeonSessionStateConflictException(dungeonSessionId, locked.Value.State);

        long currentGeneration;
        long confirmedGeneration;
        long disconnectedGeneration;
        DateTimeOffset? disconnectedAt;
        DateTimeOffset leaseExpiresAt;
        await using (NpgsqlCommand command = new("""
            SELECT connection_generation, confirmed_connection_generation,
                   disconnected_generation, disconnected_at, lease_expires_at
            FROM dungeon_session_members
            WHERE dungeon_session_id = @session_id
              AND character_id = @character_id AND steam_id = @steam_id;
            """, connection, transaction))
        {
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken))
                throw new DungeonSessionMembershipException(dungeonSessionId);
            currentGeneration = reader.GetInt64(0);
            confirmedGeneration = reader.GetInt64(1);
            disconnectedGeneration = reader.GetInt64(2);
            disconnectedAt = reader.IsDBNull(3) ? null : reader.GetFieldValue<DateTimeOffset>(3);
            leaseExpiresAt = reader.GetFieldValue<DateTimeOffset>(4);
        }
        if (connectionGeneration <= 0 || currentGeneration != connectionGeneration || leaseExpiresAt <= now)
            throw new DungeonSessionMembershipException(dungeonSessionId);
        if (confirmedGeneration == connectionGeneration)
        {
            await transaction.CommitAsync(cancellationToken);
            return disconnectedAt is null;
        }
        if (disconnectedAt is not null && disconnectedGeneration == connectionGeneration)
        {
            await transaction.CommitAsync(cancellationToken);
            return false;
        }
        if (DungeonConnectionPolicy.HasReconnectGraceExpired(disconnectedAt, now))
        {
            await transaction.CommitAsync(cancellationToken);
            return false;
        }

        await using (NpgsqlCommand command = new("""
            UPDATE dungeon_session_members
            SET disconnected_at = NULL,
                confirmed_connection_generation = @connection_generation,
                disconnected_generation = 0
            WHERE dungeon_session_id = @session_id
              AND character_id = @character_id AND steam_id = @steam_id;
            """, connection, transaction))
        {
            command.Parameters.AddWithValue("session_id", dungeonSessionId);
            command.Parameters.AddWithValue("character_id", characterId);
            command.Parameters.AddWithValue("steam_id", steamId);
            command.Parameters.AddWithValue("connection_generation", connectionGeneration);
            await command.ExecuteNonQueryAsync(cancellationToken);
        }
        await transaction.CommitAsync(cancellationToken);
        return true;
    }

    public Task<DungeonSession> AssignDungeonServerAsync(
        Guid dungeonSessionId,
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken,
        bool requireDeparture = false,
        int minimumPartySize = 1) =>
        TransitionDungeonSessionAsync(
            dungeonSessionId,
            serverId,
            DungeonSessionState.Waiting,
            DungeonSessionState.Loading,
            serverAddress,
            now,
            leaseExpiresAt,
            cancellationToken,
            requireDeparture,
            minimumPartySize);

    public async Task<DungeonSession?> ClaimNextDungeonSessionAsync(
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken,
        IReadOnlyList<DungeonContentSelection>? allowedSelections = null,
        bool requireDeparture = false,
        int minimumPartySize = 1)
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
              AND (NOT @require_departure OR (
                  s.departure_requested_at IS NOT NULL AND s.leader_character_id IS NOT NULL
                  AND EXISTS (SELECT 1 FROM dungeon_session_members leader
                      WHERE leader.dungeon_session_id = s.dungeon_session_id
                        AND leader.character_id = s.leader_character_id AND leader.lease_expires_at > @now)
                  AND (SELECT COUNT(*) FROM dungeon_session_members party
                      WHERE party.dungeon_session_id = s.dungeon_session_id AND party.lease_expires_at > @now) >= @minimum_party_size
                  AND NOT EXISTS (SELECT 1 FROM dungeon_session_members expired
                      WHERE expired.dungeon_session_id = s.dungeon_session_id AND expired.lease_expires_at <= @now)))
              AND (@unrestricted OR EXISTS (
                  SELECT 1 FROM unnest(@allowed_ids, @allowed_difficulties) AS content(id, difficulty)
                  WHERE content.id = s.dungeon_id AND content.difficulty = s.difficulty))
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
            claimCommand.Parameters.AddWithValue("require_departure", requireDeparture);
            claimCommand.Parameters.AddWithValue("minimum_party_size", Math.Clamp(minimumPartySize, 1, 8));
            claimCommand.Parameters.AddWithValue("unrestricted", allowedSelections is null);
            claimCommand.Parameters.AddWithValue("allowed_ids",
                allowedSelections?.Select(pair => pair.DungeonId).ToArray() ?? Array.Empty<string>());
            claimCommand.Parameters.AddWithValue("allowed_difficulties",
                allowedSelections?.Select(pair => pair.Difficulty).ToArray() ?? Array.Empty<string>());
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

        // Snapshot eligibility only after the shared session lock is held.
        // Never recalculate an existing immutable settlement on a later retry.
        now = connectionClock?.GetUtcNow() ?? now;
        List<Guid> characterIds = [];
        int memberCount = 0;
        await using (NpgsqlCommand readMembers = new(
            """
            SELECT character_id, disconnected_at
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
                memberCount++;
                DateTimeOffset? disconnectedAt = reader.IsDBNull(1) ? null : reader.GetFieldValue<DateTimeOffset>(1);
                if (!DungeonConnectionPolicy.HasReconnectGraceExpired(disconnectedAt, now))
                    characterIds.Add(reader.GetGuid(0));
            }
        }

        // A real party whose every reconnect deadline expired has an empty
        // payout, not a retryable settlement failure. Keep the original members.
        if (memberCount == 0)
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
                JsonSerializer.Serialize(DungeonPersonalLootSelection.Capture(itemRewards), SettlementJsonOptions));
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
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        return await IsAuthorizedGameServerSessionMemberAsync(connection, null, dungeonSessionId,
            serverId, steamId, characterId, now, cancellationToken);
    }

    private static async Task<bool> IsAuthorizedGameServerSessionMemberAsync(
        NpgsqlConnection connection, NpgsqlTransaction? transaction, Guid dungeonSessionId,
        string serverId, string steamId, Guid characterId, DateTimeOffset now, CancellationToken cancellationToken)
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
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("server_id", serverId);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue("now", now);
        return (bool)(await command.ExecuteScalarAsync(cancellationToken))!;
    }

    public async Task<bool> AreAuthorizedSecurityTelemetryMembersAsync(
        Guid dungeonSessionId,
        string serverId,
        IReadOnlyList<SecurityTelemetryMember> members,
        DateTimeOffset now,
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken)
    {
        SecurityTelemetryMember[] requested = members
            .Distinct()
            .ToArray();
        if (requested.Length == 0)
        {
            return false;
        }

        const string sql = """
            WITH requested(character_id, steam_id) AS (
                SELECT *
                FROM unnest(@character_ids::UUID[], @steam_ids::TEXT[])
            ), session_scope AS (
                SELECT state, expires_at, updated_at
                FROM dungeon_sessions
                WHERE dungeon_session_id = @dungeon_session_id
                  AND server_id = @server_id
            )
            SELECT COUNT(*) = @member_count
            FROM requested r
            JOIN dungeon_session_members m
              ON m.dungeon_session_id = @dungeon_session_id
             AND m.character_id = r.character_id
             AND m.steam_id = r.steam_id
            CROSS JOIN session_scope s
            LEFT JOIN character_session_leases l
              ON l.character_id = r.character_id
             AND l.dungeon_session_id = @dungeon_session_id
            WHERE (
                    s.state IN ('Loading', 'InProgress')
                AND s.expires_at > @now
                AND m.lease_expires_at > @now
                AND l.expires_at > @now)
               OR (
                    s.state IN ('Cleared', 'Failed')
                AND s.updated_at >= @post_session_cutoff);
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue(
            "character_ids",
            requested.Select(value => value.CharacterId).ToArray());
        command.Parameters.AddWithValue(
            "steam_ids",
            requested.Select(value => value.SteamId).ToArray());
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("server_id", serverId);
        command.Parameters.AddWithValue("member_count", requested.Length);
        command.Parameters.AddWithValue("now", now);
        command.Parameters.AddWithValue(
            "post_session_cutoff",
            now - postSessionGrace);
        return (bool)(await command.ExecuteScalarAsync(cancellationToken))!;
    }

    public async Task<JoinTicketIssueResult> StoreJoinTicketAsync(
        string tokenHash,
        string steamId,
        Guid characterId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        await PostgresAdmissionAccess.EnsureAsync(connection, transaction, steamId, connectionClock?.GetUtcNow() ?? DateTimeOffset.UtcNow, cancellationToken);
        var locked = await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken);
        if (locked is null) return JoinTicketIssueResult.NotActiveMember;
        DateTimeOffset now = connectionClock?.GetUtcNow() ?? DateTimeOffset.UtcNow;

        // Finalized expulsions remain distinguishable after member removal/Closed.
        // Never disclose another account's receipt or infer expiry from absence.
        await using (NpgsqlCommand receipt = new("""
            SELECT EXISTS (SELECT 1 FROM dungeon_session_disconnection_expulsions
                WHERE dungeon_session_id = @session_id AND character_id = @character_id AND steam_id = @steam_id);
            """, connection, transaction))
        {
            receipt.Parameters.AddWithValue("session_id", dungeonSessionId);
            receipt.Parameters.AddWithValue("character_id", characterId);
            receipt.Parameters.AddWithValue("steam_id", steamId);
            if ((bool)(await receipt.ExecuteScalarAsync(cancellationToken))!)
                return JoinTicketIssueResult.ReconnectGraceExpired;
        }
        if (locked.Value.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress)
            || locked.Value.ServerId is null || expiresAt <= now)
            return JoinTicketIssueResult.NotActiveMember;

        await using (NpgsqlCommand member = new("""
            SELECT m.disconnected_at, m.lease_expires_at > @now AND EXISTS (
                SELECT 1 FROM character_session_leases l WHERE l.character_id = m.character_id
                AND l.dungeon_session_id = m.dungeon_session_id AND l.expires_at > @now)
            FROM dungeon_session_members m JOIN dungeon_sessions s USING (dungeon_session_id)
            WHERE m.dungeon_session_id = @session_id AND m.character_id = @character_id
              AND m.steam_id = @steam_id AND s.expires_at > @now;
            """, connection, transaction))
        {
            member.Parameters.AddWithValue("session_id", dungeonSessionId);
            member.Parameters.AddWithValue("character_id", characterId);
            member.Parameters.AddWithValue("steam_id", steamId);
            member.Parameters.AddWithValue("now", now);
            await using NpgsqlDataReader reader = await member.ExecuteReaderAsync(cancellationToken);
            if (!await reader.ReadAsync(cancellationToken)) return JoinTicketIssueResult.NotActiveMember;
            DateTimeOffset? disconnectedAt = reader.IsDBNull(0) ? null : reader.GetFieldValue<DateTimeOffset>(0);
            if (DungeonConnectionPolicy.HasReconnectGraceExpired(disconnectedAt, now))
                return JoinTicketIssueResult.ReconnectGraceExpired;
            if (!reader.GetBoolean(1)) return JoinTicketIssueResult.NotActiveMember;
        }

        const string sql = """
            DELETE FROM game_join_tickets
            WHERE dungeon_session_id = @dungeon_session_id AND expires_at <= @now;

            INSERT INTO game_join_tickets (
                token_hash, steam_id, character_id,
                dungeon_session_id, expires_at)
            VALUES (
                @token_hash, @steam_id, @character_id,
                @dungeon_session_id, @expires_at);
            """;

        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("steam_id", steamId);
        command.Parameters.AddWithValue("character_id", characterId);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("expires_at", expiresAt);
        command.Parameters.AddWithValue("now", now);
        await command.ExecuteNonQueryAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);
        return JoinTicketIssueResult.Issued;
    }

    public async Task<ConsumedJoinTicket?> ConsumeJoinTicketAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken,
        Guid? consumptionRequestId = null)
    {
        if (consumptionRequestId == Guid.Empty) return null;
        // Serialize admission against abandonment/settlement before validating
        // the member and lease. A ticket issued late still cannot restore a member.
        const string sql = """
            DELETE FROM game_join_tickets t
            USING dungeon_sessions s,
                  dungeon_session_members m,
                  character_session_leases l
            WHERE t.token_hash = @token_hash
              AND NOT EXISTS (SELECT 1 FROM game_join_ticket_consumptions r WHERE r.token_hash = t.token_hash)
              AND NOT EXISTS (SELECT 1 FROM game_join_ticket_consumption_cancellations c
                  WHERE c.token_hash = t.token_hash OR (c.dungeon_session_id = @dungeon_session_id
                      AND c.server_id = @server_id AND c.consumption_request_id = @request_id))
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
              AND (m.disconnected_at IS NULL OR @now - m.disconnected_at < @reconnect_grace)
              AND m.connection_generation < @max_generation
              AND l.character_id = t.character_id
              AND l.dungeon_session_id = s.dungeon_session_id
              AND l.expires_at > @now
            RETURNING t.dungeon_session_id, t.character_id, t.steam_id;
            """;

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        string? account;
        await using (NpgsqlCommand owner = new("""
            SELECT steam_id FROM game_join_tickets WHERE token_hash=@hash
            UNION SELECT steam_id FROM game_join_ticket_consumptions WHERE token_hash=@hash LIMIT 1
            """, connection, transaction))
        { owner.Parameters.AddWithValue("hash",tokenHash); account = await owner.ExecuteScalarAsync(cancellationToken) as string; }
        if (account is null || !await PostgresAdmissionAccess.AllowAsync(connection,transaction,account,
            connectionClock?.GetUtcNow() ?? now,cancellationToken)) return null;
        if (await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken) is null)
            return null;
        now = connectionClock?.GetUtcNow() ?? now;
        // Admission replay expires; consumption evidence does not. Retain it
        // for late, read-only reconciliation instead of inventing a missing commit.
        if (consumptionRequestId is Guid requestId)
        {
            await using NpgsqlCommand recovery = new("""
                SELECT r.token_hash, r.character_id, r.steam_id, r.connection_generation,
                    EXISTS (
                        SELECT 1 FROM dungeon_sessions s
                        JOIN dungeon_session_members m ON m.dungeon_session_id = s.dungeon_session_id
                        JOIN character_session_leases l ON l.character_id = m.character_id
                        WHERE s.dungeon_session_id = r.dungeon_session_id AND s.server_id = r.server_id
                          AND r.recovery_expires_at > @now
                          AND s.state IN ('Loading', 'InProgress') AND s.expires_at > @now
                          AND m.character_id = r.character_id AND m.steam_id = r.steam_id
                          AND m.connection_generation = r.connection_generation AND m.lease_expires_at > @now
                          AND (m.disconnected_at IS NULL OR @now - m.disconnected_at < @reconnect_grace)
                          AND l.dungeon_session_id = s.dungeon_session_id AND l.expires_at > @now
                    ) AS current_admission
                FROM game_join_ticket_consumptions r
                WHERE r.dungeon_session_id = @session_id AND r.server_id = @server_id
                  AND r.consumption_request_id = @request_id;
                """, connection, transaction);
            recovery.Parameters.AddWithValue("session_id", dungeonSessionId);
            recovery.Parameters.AddWithValue("server_id", serverId);
            recovery.Parameters.AddWithValue("request_id", requestId);
            recovery.Parameters.AddWithValue("now", now);
            recovery.Parameters.AddWithValue("reconnect_grace", DungeonConnectionPolicy.UnexpectedDisconnectGrace);
            bool found;
            ConsumedJoinTicket? recovered = null;
            await using (NpgsqlDataReader reader = await recovery.ExecuteReaderAsync(cancellationToken))
            {
                found = await reader.ReadAsync(cancellationToken);
                if (found && reader.GetString(0).TrimEnd() == tokenHash && reader.GetBoolean(4))
                    recovered = new(dungeonSessionId, reader.GetGuid(1), reader.GetString(2), reader.GetInt64(3));
            }
            if (found)
            {
                await transaction.CommitAsync(cancellationToken);
                return recovered;
            }
        }
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("token_hash", tokenHash);
        command.Parameters.AddWithValue("server_id", serverId);
        command.Parameters.AddWithValue("request_id", consumptionRequestId ?? Guid.Empty);
        command.Parameters.AddWithValue(
            "dungeon_session_id",
            dungeonSessionId);
        command.Parameters.AddWithValue("now", now);
        command.Parameters.AddWithValue("reconnect_grace", DungeonConnectionPolicy.UnexpectedDisconnectGrace);
        command.Parameters.AddWithValue("max_generation", long.MaxValue);
        ConsumedJoinTicket? result = null;
        await using (NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken))
        {
            if (await reader.ReadAsync(cancellationToken))
                result = new ConsumedJoinTicket(reader.GetGuid(0), reader.GetGuid(1), reader.GetString(2), 0);
        }
        if (result is not null)
        {
            // Ticket consumption reserves a new admission generation, but it is
            // not proof that InitNewPlayer succeeded: keep disconnected_at until
            // a later admission confirmation can safely clear it.
            await using NpgsqlCommand update = new("""
                UPDATE dungeon_session_members
                SET connection_generation = connection_generation + 1
                WHERE dungeon_session_id = @session_id AND character_id = @character_id
                  AND steam_id = @steam_id AND connection_generation < @max_generation
                RETURNING connection_generation;
                """, connection, transaction);
            update.Parameters.AddWithValue("session_id", dungeonSessionId);
            update.Parameters.AddWithValue("character_id", result.CharacterId);
            update.Parameters.AddWithValue("steam_id", result.SteamId);
            update.Parameters.AddWithValue("max_generation", long.MaxValue);
            object? generation = await update.ExecuteScalarAsync(cancellationToken);
            if (generation is not long nextGeneration)
                throw new DungeonSessionMembershipException(dungeonSessionId);
            result = result with { ConnectionGeneration = nextGeneration };
            if (consumptionRequestId is Guid id)
            {
                await using NpgsqlCommand receipt = new("""
                    INSERT INTO game_join_ticket_consumptions (
                        token_hash, consumption_request_id, server_id, dungeon_session_id,
                        character_id, steam_id, connection_generation, consumed_at, recovery_expires_at)
                    VALUES (@hash, @request_id, @server_id, @session_id,
                        @character_id, @steam_id, @generation, @now, @expires);
                    """, connection, transaction);
                receipt.Parameters.AddWithValue("hash", tokenHash);
                receipt.Parameters.AddWithValue("request_id", id);
                receipt.Parameters.AddWithValue("server_id", serverId);
                receipt.Parameters.AddWithValue("session_id", dungeonSessionId);
                receipt.Parameters.AddWithValue("character_id", result.CharacterId);
                receipt.Parameters.AddWithValue("steam_id", result.SteamId);
                receipt.Parameters.AddWithValue("generation", result.ConnectionGeneration);
                receipt.Parameters.AddWithValue("now", now);
                receipt.Parameters.AddWithValue("expires", now + JoinTicketConsumptionRecovery.Retention);
                await receipt.ExecuteNonQueryAsync(cancellationToken);
            }
        }
        await transaction.CommitAsync(cancellationToken);
        return result;
    }

    public async Task<JoinTicketConsumptionEvidence?> ReadJoinTicketConsumptionEvidenceAsync(
        string tokenHash, string serverId, Guid dungeonSessionId, Guid consumptionRequestId,
        string expectedSteamId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        if (consumptionRequestId == Guid.Empty) return null;
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);
        if (await LockDungeonSessionAsync(connection, transaction, dungeonSessionId, cancellationToken) is null)
            return null;
        now = connectionClock?.GetUtcNow() ?? now;
        await using NpgsqlCommand command = new("""
            SELECT r.character_id, r.steam_id, r.connection_generation, r.consumed_at
            FROM game_join_ticket_consumptions r
            JOIN dungeon_sessions s ON s.dungeon_session_id = r.dungeon_session_id
            WHERE r.dungeon_session_id = @session_id AND r.server_id = @server_id
              AND r.consumption_request_id = @request_id AND r.token_hash = @hash AND r.steam_id = @steam_id
              AND s.server_id = @server_id AND s.state IN ('Loading', 'InProgress') AND s.expires_at > @now;
            """, connection, transaction);
        command.Parameters.AddWithValue("session_id", dungeonSessionId);
        command.Parameters.AddWithValue("server_id", serverId);
        command.Parameters.AddWithValue("request_id", consumptionRequestId);
        command.Parameters.AddWithValue("hash", tokenHash);
        command.Parameters.AddWithValue("steam_id", expectedSteamId);
        command.Parameters.AddWithValue("now", now);
        JoinTicketConsumptionEvidence? evidence = null;
        await using (NpgsqlDataReader reader = await command.ExecuteReaderAsync(cancellationToken))
        {
            if (await reader.ReadAsync(cancellationToken))
                evidence = new(dungeonSessionId, serverId, consumptionRequestId, reader.GetGuid(0),
                    reader.GetString(1), reader.GetInt64(2), reader.GetFieldValue<DateTimeOffset>(3));
        }
        await transaction.CommitAsync(cancellationToken);
        return evidence;
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
        CancellationToken cancellationToken,
        GameServerCredential? requiredServer = null)
    {
        await using NpgsqlConnection connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync(cancellationToken);

        if (requiredServer is not null)
        {
            // All member/assignment changes lock this parent. Keep authorization
            // and DELETE/INSERT in the same transaction, not an API-only precheck.
            _ = await LockDungeonSessionAsync(connection, transaction, requiredServer.DungeonSessionId, cancellationToken);
            await using NpgsqlCommand ownerQuery = new(
                "SELECT steam_id FROM characters WHERE character_id = @character_id;", connection, transaction);
            ownerQuery.Parameters.AddWithValue("character_id", characterId);
            string? owner = await ownerQuery.ExecuteScalarAsync(cancellationToken) as string;
            if (requiredServer.IsSecurityTelemetryOnly || owner is null
                || !await IsAuthorizedGameServerSessionMemberAsync(connection, transaction,
                    requiredServer.DungeonSessionId, requiredServer.ServerId, owner, characterId, updatedAt, cancellationToken))
                throw new DungeonSessionMembershipException(requiredServer.DungeonSessionId);
        }

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
        CancellationToken cancellationToken,
        bool requireDeparture = false,
        int minimumPartySize = 1)
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

        if (requireDeparture && locked.Value.State == DungeonSessionState.Waiting
            && !DungeonDeparturePolicy.CanAllocate((await ReadDungeonSessionAsync(connection, transaction,
                dungeonSessionId, cancellationToken))!, minimumPartySize, now))
            throw new DungeonDepartureConflictException("dungeon_departure_not_requested");
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
        Guid? leaderCharacterId;
        long lobbyRevision;
        Guid? departureRequestId;
        long? departureRequestRevision;
        DateTimeOffset? departureRequestedAt;

        await using (NpgsqlCommand command = new(
            """
            SELECT dungeon_id, difficulty, state, server_id,
                   server_address, created_at, updated_at, expires_at,
                   leader_character_id, lobby_revision, departure_request_id,
                   departure_request_revision, departure_requested_at
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
            leaderCharacterId = reader.IsDBNull(8) ? null : reader.GetGuid(8);
            lobbyRevision = reader.GetInt64(9);
            departureRequestId = reader.IsDBNull(10) ? null : reader.GetGuid(10);
            departureRequestRevision = reader.IsDBNull(11) ? null : reader.GetInt64(11);
            departureRequestedAt = reader.IsDBNull(12) ? null : reader.GetFieldValue<DateTimeOffset>(12);
        }

        List<DungeonSessionMember> members = [];
        await using (NpgsqlCommand command = new(
            """
            SELECT m.character_id, m.steam_id, m.joined_at, m.lease_expires_at, m.disconnected_at,
                   m.connection_generation, m.confirmed_connection_generation, m.disconnected_generation,
                   c.name
            FROM dungeon_session_members m
            LEFT JOIN characters c ON c.character_id = m.character_id AND c.steam_id = m.steam_id
            WHERE m.dungeon_session_id = @dungeon_session_id
            ORDER BY m.joined_at, m.character_id;
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
                    reader.GetFieldValue<DateTimeOffset>(3),
                    reader.IsDBNull(4) ? null : reader.GetFieldValue<DateTimeOffset>(4),
                    reader.GetInt64(5),
                    reader.GetInt64(6),
                    reader.GetInt64(7),
                    reader.IsDBNull(8) ? null : reader.GetString(8)));
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
            members, leaderCharacterId, lobbyRevision, departureRequestId, departureRequestRevision, departureRequestedAt);
    }
}
