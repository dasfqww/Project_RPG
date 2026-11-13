using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : IRidingRegistrationStore
{
    public async Task<RidingRegistrationResult> RegisterMountAsync(string authenticatedSteamId, Guid characterId,
        RidingRegistrationCommand command, RidingCatalog catalog, CancellationToken cancellationToken)
    {
        for (int attempt = 0; ; attempt++)
        {
            try { return await RegisterRidingTransactionAsync(authenticatedSteamId, characterId, command, catalog, cancellationToken); }
            catch (PostgresException e) when (attempt < 2 && e.SqlState is PostgresErrorCodes.DeadlockDetected or PostgresErrorCodes.SerializationFailure)
            { await Task.Delay(Random.Shared.Next(15, 60), cancellationToken); }
        }
    }

    private async Task<RidingRegistrationResult> RegisterRidingTransactionAsync(string account, Guid characterId,
        RidingRegistrationCommand command, RidingCatalog catalog, CancellationToken ct)
    {
        var items = equipmentItems ?? throw new InvalidOperationException("Riding requires the injected item repository.");
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await connection.BeginTransactionAsync(ct);
        var actor = await LockRidingOwnerAsync(connection, tx, account, characterId, ct);
        string fingerprint = RidingRegistrationRules.Fingerprint(actor, command);
        var original = await ReadRidingReceiptAsync(connection, tx, actor, command.RequestId, ct);
        if (original is not null) return await ReplayRidingAsync(tx, original, fingerprint, ct);
        await PostgresServiceAccess.EnsureOpenAsync(connection, tx, ct);
        await EnsureRidingLobbyAsync(connection, tx, characterId, ct);
        await using (var gate = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", connection, tx))
        {
            gate.Parameters.AddWithValue("key", "riding_request:" + command.RequestId.ToString("D")); await gate.ExecuteNonQueryAsync(ct);
        }
        original = await ReadRidingReceiptAsync(connection, tx, actor, command.RequestId, ct);
        if (original is not null) return await ReplayRidingAsync(tx, original, fingerprint, ct);
        await using (var state = new NpgsqlCommand("""
            INSERT INTO riding_roster_state(roster_id,revision) VALUES(@roster,0) ON CONFLICT DO NOTHING;
            SELECT revision FROM riding_roster_state WHERE roster_id=@roster FOR UPDATE;
            """, connection, tx))
        {
            state.Parameters.AddWithValue("roster", actor.RosterId); await state.ExecuteNonQueryAsync(ct);
        }
        var collection = await ReadRidingCollectionAsync(connection, tx, actor.RosterId, ct);
        Guid childId = RidingRegistrationRules.ChildItemRequestId(command.RequestId);
        var item = await PostgresItemRepository.LockRidingItemAsync(connection, tx, characterId, command.ItemId, childId, ct)
            ?? throw new RidingRuleException(RidingFailure.ItemUnavailable);
        var reserved = await PostgresTradeReservationGuards.ReservedItems(connection, tx, [item.ItemId], ct);
        DateTimeOffset now = await RidingDbNowAsync(connection, tx, ct);
        var plan = RidingRegistrationRules.Plan(actor, command, catalog, item, collection.Revision,
            collection.Mounts.Select(m => m.MountId).ToHashSet(StringComparer.Ordinal), reserved.Contains(item.ItemId), now);
        ItemRepositoryCommitResult? child = null;
        if (plan.Consumption is { } consumption)
        {
            if (await PostgresItemRepository.ReadRidingChildAsync(connection, tx, childId, ct) is not null)
                throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
            child = await items.CommitInTransactionAsync(connection, tx,
                new(childId, RidingRegistrationRules.Operation, fingerprint, item.Owner, 1, [consumption]), now, ct);
            if (child.Status != ItemRepositoryCommitStatus.Committed) throw new RidingStoreException(RidingStoreFailure.ItemCommitFailed);
            await using var unlock = new NpgsqlCommand("""
                INSERT INTO riding_mount_unlocks(roster_id,mount_id,definition_version,unlocked_at_utc,source_request_id)
                  VALUES(@roster,@mount,@version,@now,@request);
                UPDATE riding_roster_state SET revision=@next WHERE roster_id=@roster AND revision=@expected;
                """, connection, tx);
            unlock.Parameters.AddWithValue("roster", actor.RosterId); unlock.Parameters.AddWithValue("mount", plan.MountId);
            unlock.Parameters.AddWithValue("version", plan.Unlock!.DefinitionVersion); unlock.Parameters.AddWithValue("now", now);
            unlock.Parameters.AddWithValue("request", command.RequestId); unlock.Parameters.AddWithValue("next", plan.NewRosterRevision);
            unlock.Parameters.AddWithValue("expected", plan.ExpectedRosterRevision);
            if (await unlock.ExecuteNonQueryAsync(ct) != 2) throw new RidingStoreException(RidingStoreFailure.ItemCommitFailed);
        }
        var receipt = new RidingRegistrationReceipt(command.RequestId, RidingRegistrationRules.Operation, fingerprint,
            characterId, actor.RosterId, plan.Outcome, plan.MountId, plan.NewRosterRevision, now, child);
        await using (var insert = new NpgsqlCommand("""
            INSERT INTO riding_command_receipts(request_id,operation,character_id,roster_id,canonical_fingerprint,outcome,
              result_payload,committed_at_utc,child_item_request_id) VALUES(@request,@op,@character,@roster,@fingerprint,@outcome,@payload,@now,@child);
            """, connection, tx))
        {
            insert.Parameters.AddWithValue("request", command.RequestId); insert.Parameters.AddWithValue("op", receipt.Operation);
            insert.Parameters.AddWithValue("character", characterId); insert.Parameters.AddWithValue("roster", actor.RosterId);
            insert.Parameters.AddWithValue("fingerprint", fingerprint); insert.Parameters.AddWithValue("outcome", receipt.Outcome.ToString());
            insert.Parameters.AddWithValue("payload", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(receipt)); insert.Parameters.AddWithValue("now", now);
            insert.Parameters.AddWithValue("child", NpgsqlDbType.Uuid, (object?)plan.ChildItemRequestId ?? DBNull.Value);
            await insert.ExecuteNonQueryAsync(ct);
        }
        await tx.CommitAsync(ct);
        return new(false, receipt);
    }

    public async Task<RidingCollection> GetRidingCollectionAsync(string authenticatedSteamId, Guid characterId, CancellationToken cancellationToken)
    {
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using var tx = await connection.BeginTransactionAsync(cancellationToken);
        var actor = await LockRidingOwnerAsync(connection, tx, authenticatedSteamId, characterId, cancellationToken);
        var result = await ReadRidingCollectionAsync(connection, tx, actor.RosterId, cancellationToken);
        await tx.CommitAsync(cancellationToken); return result;
    }

    public async Task<RidingRegistrationReceipt?> GetRidingRegistrationReceiptAsync(string authenticatedSteamId, Guid characterId,
        Guid requestId, CancellationToken cancellationToken)
    {
        if (requestId == Guid.Empty) throw new RidingRuleException(RidingFailure.InvalidRequest);
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        await using var tx = await connection.BeginTransactionAsync(cancellationToken);
        var actor = await LockRidingOwnerAsync(connection, tx, authenticatedSteamId, characterId, cancellationToken);
        var result = await ReadRidingReceiptAsync(connection, tx, actor, requestId, cancellationToken);
        await tx.CommitAsync(cancellationToken); return result;
    }

    private static async Task<RidingRegistrationActor> LockRidingOwnerAsync(NpgsqlConnection c, NpgsqlTransaction tx, string account, Guid character, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(account)) throw new RidingStoreException(RidingStoreFailure.CharacterNotOwned);
        // Service -> account is the existing admission/operations order. Replay takes the shared gate without requiring Open.
        await PostgresServiceAccess.LockAsync(c, tx, false, ct);
        await PostgresAccountAccess.LockAsync(c, tx, account, ct);
        await PostgresAccountAccess.EnsureAsync(c, tx, account, await RidingDbNowAsync(c, tx, ct), ct);
        await using var owner = new NpgsqlCommand("""
            SELECT ch.roster_id FROM characters ch JOIN rosters r ON r.roster_id=ch.roster_id
              WHERE ch.character_id=@character AND ch.steam_id=@account AND r.steam_id=@account FOR UPDATE OF ch;
            """, c, tx);
        owner.Parameters.AddWithValue("character", character); owner.Parameters.AddWithValue("account", account);
        return await owner.ExecuteScalarAsync(ct) is Guid roster ? new(character, roster) : throw new RidingStoreException(RidingStoreFailure.CharacterNotOwned);
    }

    private static async Task EnsureRidingLobbyAsync(NpgsqlConnection c, NpgsqlTransaction tx, Guid character, CancellationToken ct)
    {
        await using (var lease = new NpgsqlCommand("SELECT expires_at>clock_timestamp() FROM character_session_leases WHERE character_id=@id FOR UPDATE NOWAIT", c, tx))
        {
            lease.Parameters.AddWithValue("id", character);
            try { if (await lease.ExecuteScalarAsync(ct) is true) throw new RidingStoreException(RidingStoreFailure.RequiresLobby); }
            catch (PostgresException e) when (e.SqlState == PostgresErrorCodes.LockNotAvailable) { throw new RidingStoreException(RidingStoreFailure.RequiresLobby); }
        }
        await using var busy = new NpgsqlCommand("""
            SELECT EXISTS(SELECT 1 FROM dungeon_session_members m JOIN dungeon_sessions s USING(dungeon_session_id)
              WHERE m.character_id=@id AND s.state IN ('Waiting','Loading','InProgress','SettlementPending'))
            OR EXISTS(SELECT 1 FROM dungeon_reward_settlements WHERE character_ids @> @member::jsonb AND state IN ('Pending','Processing'));
            """, c, tx);
        busy.Parameters.AddWithValue("id", character); busy.Parameters.AddWithValue("member", JsonSerializer.Serialize(new[] { character }));
        if (await busy.ExecuteScalarAsync(ct) is true) throw new RidingStoreException(RidingStoreFailure.RequiresLobby);
    }

    private static async Task<RidingCollection> ReadRidingCollectionAsync(NpgsqlConnection c, NpgsqlTransaction tx, Guid roster, CancellationToken ct)
    {
        await using var query = new NpgsqlCommand("""
            SELECT revision FROM riding_roster_state WHERE roster_id=@id;
            SELECT mount_id,definition_version,unlocked_at_utc,source_request_id FROM riding_mount_unlocks WHERE roster_id=@id ORDER BY mount_id;
            """, c, tx);
        query.Parameters.AddWithValue("id", roster); await using var reader = await query.ExecuteReaderAsync(ct);
        long revision = await reader.ReadAsync(ct) ? reader.GetInt64(0) : 0;
        await reader.NextResultAsync(ct); var mounts = new List<RidingUnlock>();
        while (await reader.ReadAsync(ct)) mounts.Add(new(reader.GetString(0), reader.GetInt32(1), reader.GetFieldValue<DateTimeOffset>(2), reader.GetGuid(3)));
        return new(roster, revision, mounts.AsReadOnly());
    }

    private static async Task<RidingRegistrationReceipt?> ReadRidingReceiptAsync(NpgsqlConnection c, NpgsqlTransaction tx,
        RidingRegistrationActor actor, Guid request, CancellationToken ct)
    {
        RidingRegistrationReceipt receipt;
        await using (var query = new NpgsqlCommand("SELECT result_payload,operation,character_id,roster_id,canonical_fingerprint,outcome,child_item_request_id FROM riding_command_receipts WHERE request_id=@id", c, tx))
        {
            query.Parameters.AddWithValue("id", request); await using var reader = await query.ExecuteReaderAsync(ct);
            if (!await reader.ReadAsync(ct)) return null;
            if (reader.GetGuid(2) != actor.CharacterId || reader.GetGuid(3) != actor.RosterId) throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
            receipt = JsonSerializer.Deserialize<RidingRegistrationReceipt>(reader.GetString(0)) ?? throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
            if (receipt.RequestId != request || receipt.Operation != reader.GetString(1) || receipt.Operation != RidingRegistrationRules.Operation
                || receipt.CharacterId != actor.CharacterId || receipt.RosterId != actor.RosterId || receipt.Fingerprint != reader.GetString(4)
                || receipt.Outcome.ToString() != reader.GetString(5) || receipt.ItemReceipt?.RequestId != (reader.IsDBNull(6) ? (Guid?)null : reader.GetGuid(6)))
                throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        }
        if (receipt.Outcome == RidingRegistrationOutcome.Registered)
        {
            var child = await PostgresItemRepository.ReadRidingChildAsync(c, tx, RidingRegistrationRules.ChildItemRequestId(request), ct);
            if (child is null || receipt.ItemReceipt is null || JsonSerializer.Serialize(child) != JsonSerializer.Serialize(receipt.ItemReceipt))
                throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        }
        else if (receipt.Outcome != RidingRegistrationOutcome.AlreadyOwned || receipt.ItemReceipt is not null) throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        return receipt;
    }

    private static async Task<RidingRegistrationResult> ReplayRidingAsync(NpgsqlTransaction tx, RidingRegistrationReceipt receipt, string fingerprint, CancellationToken ct)
    {
        if (receipt.Fingerprint != fingerprint) throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
        await tx.CommitAsync(ct); return new(true, receipt);
    }

    private static async Task<DateTimeOffset> RidingDbNowAsync(NpgsqlConnection c, NpgsqlTransaction tx, CancellationToken ct)
    {
        await using var now = new NpgsqlCommand("SELECT clock_timestamp()", c, tx); return (DateTimeOffset)(await now.ExecuteScalarAsync(ct) is DateTime time
            ? new DateTimeOffset(time) : throw new InvalidDataException("Missing DB clock"));
    }
}
