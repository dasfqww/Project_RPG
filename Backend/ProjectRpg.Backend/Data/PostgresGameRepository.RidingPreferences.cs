using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : IRidingPreferencesStore
{
    public async Task<RidingPreferencesResult> SaveRidingPreferencesAsync(string authenticatedSteamId, Guid characterId,
        RidingPreferencesCommand command, RidingCatalog catalog, CancellationToken cancellationToken)
    {
        command = RidingPreferencesRules.Normalize(command);
        for (int attempt = 0; ; attempt++)
        {
            try { return await SaveRidingPreferencesTransactionAsync(authenticatedSteamId, characterId, command, catalog, cancellationToken); }
            catch (PostgresException e) when (attempt < 2 && e.SqlState is PostgresErrorCodes.DeadlockDetected or PostgresErrorCodes.SerializationFailure)
            { await Task.Delay(Random.Shared.Next(15, 60), cancellationToken); }
        }
    }
    private async Task<RidingPreferencesResult> SaveRidingPreferencesTransactionAsync(string account, Guid character,
        RidingPreferencesCommand command, RidingCatalog catalog, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct); await using var tx = await c.BeginTransactionAsync(ct);
        var actor = await LockRidingOwnerAsync(c, tx, account, character, ct);
        string fingerprint = RidingPreferencesRules.Fingerprint(actor, command);
        async Task<RidingPreferencesResult?> Replay()
        {
            if (await ReadRidingCommandAsync(c, tx, actor, command.RequestId, ct) is not { } old) return null;
            if (old.Preferences is not { } receipt || receipt.Fingerprint != fingerprint) throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
            await tx.CommitAsync(ct); return new(true, receipt);
        }
        if (await Replay() is { } replay) return replay;
        await PostgresServiceAccess.EnsureOpenAsync(c, tx, ct); await EnsureRidingLobbyAsync(c, tx, character, ct);
        await using (var gate = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", c, tx))
        { gate.Parameters.AddWithValue("key", "riding_request:" + command.RequestId.ToString("D")); await gate.ExecuteNonQueryAsync(ct); }
        if (await Replay() is { } lockedReplay) return lockedReplay;
        await using (var gate = new NpgsqlCommand("INSERT INTO riding_roster_state(roster_id,revision) VALUES(@id,0) ON CONFLICT DO NOTHING; SELECT revision FROM riding_roster_state WHERE roster_id=@id FOR UPDATE", c, tx))
        { gate.Parameters.AddWithValue("id", actor.RosterId); await gate.ExecuteNonQueryAsync(ct); }
        var collection = await ReadRidingCollectionAsync(c, tx, actor.RosterId, ct);
        var current = await ReadRidingPreferencesAsync(c, tx, character, ct);
        var updated = RidingPreferencesRules.Plan(command, current.Revision, catalog, collection);
        var now = await RidingDbNowAsync(c, tx, ct);
        await using (var save = new NpgsqlCommand("""
            INSERT INTO riding_character_preferences(character_id,revision,selected_mount_id,shortcut_mode,shortcut_mount_id)
            VALUES(@id,@revision,@selected,@mode,@mount) ON CONFLICT(character_id) DO UPDATE
              SET revision=@revision,selected_mount_id=@selected,shortcut_mode=@mode,shortcut_mount_id=@mount
              WHERE riding_character_preferences.revision=@expected;
            """, c, tx))
        {
            save.Parameters.AddWithValue("id", character); save.Parameters.AddWithValue("revision", updated.Revision);
            save.Parameters.AddWithValue("expected", command.ExpectedRevision); save.Parameters.AddWithValue("mode", updated.Shortcut.Mode.ToString());
            save.Parameters.AddWithValue("selected", NpgsqlDbType.Varchar, (object?)updated.SelectedMountId ?? DBNull.Value);
            save.Parameters.AddWithValue("mount", NpgsqlDbType.Varchar, (object?)updated.Shortcut.MountId ?? DBNull.Value);
            if (await save.ExecuteNonQueryAsync(ct) != 1) throw new RidingRuleException(RidingFailure.RevisionConflict);
        }
        await using (var favorites = new NpgsqlCommand("DELETE FROM riding_character_favorites WHERE character_id=@id; INSERT INTO riding_character_favorites(character_id,mount_id) SELECT @id,unnest(@mounts)", c, tx))
        { favorites.Parameters.AddWithValue("id", character); favorites.Parameters.AddWithValue("mounts", updated.FavoriteMountIds.ToArray()); await favorites.ExecuteNonQueryAsync(ct); }
        var receipt = new RidingPreferencesReceipt(command.RequestId, RidingPreferencesRules.Operation, fingerprint, character, actor.RosterId, updated, now);
        await using (var save = new NpgsqlCommand("""
            INSERT INTO riding_command_receipts(request_id,operation,character_id,roster_id,canonical_fingerprint,outcome,result_payload,committed_at_utc,child_item_request_id)
              VALUES(@id,@op,@character,@roster,@fingerprint,'PreferencesSaved',@payload,@now,NULL);
            """, c, tx))
        {
            save.Parameters.AddWithValue("id", command.RequestId); save.Parameters.AddWithValue("op", receipt.Operation);
            save.Parameters.AddWithValue("character", character); save.Parameters.AddWithValue("roster", actor.RosterId);
            save.Parameters.AddWithValue("fingerprint", fingerprint); save.Parameters.AddWithValue("payload", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(receipt));
            save.Parameters.AddWithValue("now", now); await save.ExecuteNonQueryAsync(ct);
        }
        await tx.CommitAsync(ct); return new(false, receipt);
    }
    public async Task<RidingOwnerView> GetRidingOwnerViewAsync(string authenticatedSteamId, Guid characterId, CancellationToken cancellationToken)
    {
        await using var c = await dataSource.OpenConnectionAsync(cancellationToken); await using var tx = await c.BeginTransactionAsync(cancellationToken);
        var actor = await LockRidingOwnerAsync(c, tx, authenticatedSteamId, characterId, cancellationToken);
        // Shared account + character locks serialize both registration and settings; the two snapshots cannot tear.
        var collection = await ReadRidingCollectionAsync(c, tx, actor.RosterId, cancellationToken);
        var prefs = await ReadRidingPreferencesAsync(c, tx, characterId, cancellationToken);
        await tx.CommitAsync(cancellationToken); return new(collection, prefs);
    }
    public async Task<RidingCommandReceipt?> GetRidingCommandReceiptAsync(string authenticatedSteamId, Guid characterId, Guid requestId, CancellationToken cancellationToken)
    {
        if (requestId == Guid.Empty) throw new RidingRuleException(RidingFailure.InvalidRequest);
        await using var c = await dataSource.OpenConnectionAsync(cancellationToken); await using var tx = await c.BeginTransactionAsync(cancellationToken);
        var actor = await LockRidingOwnerAsync(c, tx, authenticatedSteamId, characterId, cancellationToken);
        var result = await ReadRidingCommandAsync(c, tx, actor, requestId, cancellationToken); await tx.CommitAsync(cancellationToken); return result;
    }
    private static async Task<RidingPreferences> ReadRidingPreferencesAsync(NpgsqlConnection c, NpgsqlTransaction tx, Guid character, CancellationToken ct)
    {
        await using var query = new NpgsqlCommand("""
            SELECT revision,selected_mount_id,shortcut_mode,shortcut_mount_id FROM riding_character_preferences WHERE character_id=@id;
            SELECT mount_id FROM riding_character_favorites WHERE character_id=@id ORDER BY mount_id;
            """, c, tx); query.Parameters.AddWithValue("id", character); await using var reader = await query.ExecuteReaderAsync(ct);
        if (!await reader.ReadAsync(ct)) return RidingPreferences.Empty;
        long revision = reader.GetInt64(0); string? selected = reader.IsDBNull(1) ? null : reader.GetString(1);
        if (!Enum.TryParse<RidingShortcutMode>(reader.GetString(2), out var mode)) throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        string? mount = reader.IsDBNull(3) ? null : reader.GetString(3);
        await reader.NextResultAsync(ct); var favorites = new List<string>(); while (await reader.ReadAsync(ct)) favorites.Add(reader.GetString(0));
        return new(revision, selected, favorites.AsReadOnly(), new(mode, mount));
    }
    private static async Task<RidingCommandReceipt?> ReadRidingCommandAsync(NpgsqlConnection c, NpgsqlTransaction tx,
        RidingRegistrationActor actor, Guid request, CancellationToken ct)
    {
        string operation;
        await using (var query = new NpgsqlCommand("SELECT operation FROM riding_command_receipts WHERE request_id=@id", c, tx))
        { query.Parameters.AddWithValue("id", request); if (await query.ExecuteScalarAsync(ct) is not string op) return null; operation = op; }
        if (operation == RidingRegistrationRules.Operation) return new(operation, await ReadRidingReceiptAsync(c, tx, actor, request, ct), null);
        if (operation != RidingPreferencesRules.Operation) throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        await using var read = new NpgsqlCommand("SELECT result_payload,character_id,roster_id,canonical_fingerprint,outcome,child_item_request_id FROM riding_command_receipts WHERE request_id=@id", c, tx);
        read.Parameters.AddWithValue("id", request); await using var reader = await read.ExecuteReaderAsync(ct);
        if (!await reader.ReadAsync(ct)) throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        if (reader.GetGuid(1) != actor.CharacterId || reader.GetGuid(2) != actor.RosterId) throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
        var receipt = JsonSerializer.Deserialize<RidingPreferencesReceipt>(reader.GetString(0)) ?? throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        if (receipt.RequestId != request || receipt.Operation != operation || receipt.CharacterId != actor.CharacterId || receipt.RosterId != actor.RosterId
            || receipt.Fingerprint != reader.GetString(3) || reader.GetString(4) != "PreferencesSaved" || !reader.IsDBNull(5) || receipt.Preferences.Revision < 1)
            throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        return new(operation, null, receipt with { Preferences = receipt.Preferences with { FavoriteMountIds = Array.AsReadOnly(receipt.Preferences.FavoriteMountIds.ToArray()) } });
    }
}
