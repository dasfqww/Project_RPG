using System.Data;
using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Shop;

public sealed class PostgresShopStore(NpgsqlDataSource dataSource) : IShopStore
{
    public async Task<ShopReceipt> ExecuteAsync(ShopCommand command, ShopCatalog catalog, CancellationToken ct)
    {
        ShopRules.Validate(command);
        var fingerprint = ShopRules.Fingerprint(command);
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        await PostgresServiceAccess.LockAsync(c, tx, false, ct);
        await PostgresAccountAccess.LockAsync(c, tx, command.Actor.AccountId, ct);
        await using (var requestGate = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", c, tx))
        {
            requestGate.Parameters.AddWithValue("key", "shop_request:" + command.RequestId.ToString("D"));
            await requestGate.ExecuteNonQueryAsync(ct);
        }
        var context = await Context(c, tx, command.Actor, true, ct);
        var existing = await Stored(c, tx, command.RequestId, command.Actor, true, ct);
        if (existing is not null)
        {
            if (existing.Value.Fingerprint != fingerprint) throw new ShopRuleException("shop_idempotency_conflict");
            if (existing.Value.Command != command) throw new InvalidDataException("Shop command mismatch.");
            return existing.Value.Outcome.Replay(command);
        }
        // Keep request, account and character locks outside the savepoint. A rejected decision must seal
        // this request while rolling back speculative zero-wallet-row creation and all later locks.
        await using (var savepoint = new NpgsqlCommand("SAVEPOINT shop_validation", c, tx))
            await savepoint.ExecuteNonQueryAsync(ct);
        bool assetWriteStarted = false;
        try
        {
            if (!catalog.Enabled) throw new ShopRuleException("shop_disabled");
            await PostgresServiceAccess.EnsureOpenAsync(c, tx, ct);
            await PostgresAccountAccess.EnsureAsync(c, tx, command.Actor.AccountId, await Now(c, tx, ct), ct);
            // Character FOR UPDATE closes the absent lease/FK insert race. NOWAIT prevents lease cleanup lock inversion.
            await using (var lease = new NpgsqlCommand("SELECT expires_at>clock_timestamp() FROM character_session_leases WHERE character_id=@id FOR UPDATE NOWAIT", c, tx))
            {
                lease.Parameters.AddWithValue("id", command.Actor.CharacterId);
                try
                {
                    if (await lease.ExecuteScalarAsync(ct) is true) throw new ShopRuleException("shop_requires_lobby");
                }
                catch (PostgresException e) when (e.SqlState == PostgresErrorCodes.LockNotAvailable)
                { throw new ShopRuleException("shop_lease_busy"); }
            }
            var owner = new ItemOwnerRef(ItemOwnerType.Character, command.Actor.CharacterId.ToString("D"));
            await PostgresItemRepository.AcquireOwnerLocksAsync(c, tx, [owner], ct);
            var owned = await Owned(c, tx, command.Actor, true, ct);
            var inventory = owned.Where(i => i.Location.ContainerType == ItemContainerType.Inventory).ToArray();
            var reserved = await PostgresTradeReservationGuards.ReservedItems(c, tx, inventory.Select(i => i.ItemId), ct);
            var definition = await PostgresEconomyRepository.ReadDefinitionAsync(c, tx, catalog.CurrencyCode, ct);
            if (definition is null || !definition.Enabled || !EconomyRules.TryValidateDefinition(definition, out _))
                throw new ShopRuleException("shop_currency_unavailable");
            var walletOwner = new CurrencyOwnerRef(definition.Scope, EconomyRules.ResolveOwnerId(definition.Scope, context));
            await PostgresEconomyRepository.EnsureBalanceRowAsync(c, tx, walletOwner, catalog.CurrencyCode, await Now(c, tx, ct), ct);
            var balance = await PostgresEconomyRepository.LockBalanceAsync(c, tx, walletOwner, catalog.CurrencyCode, ct);
            if (balance.Revision == long.MaxValue) throw new ShopRuleException("shop_revision_exhausted");
            long held = await PostgresTradeReservationGuards.ReservedCurrency(c, tx, walletOwner, catalog.CurrencyCode, ct);
            var now = await Now(c, tx, ct);
            var plan = ShopPlanner.Plan(command, catalog, inventory, reserved, balance.Balance,
                balance.Balance - held, definition.MaxBalance, now);
            if (owned.Count + plan.Mutations.Count(m => m.ExpectedRevision == 0) > 500)
                throw new ShopRuleException("shop_inventory_full");
            assetWriteStarted = true;
            var written = await Write(c, tx, plan.Mutations, now, ct);
            await using (var money = new NpgsqlCommand("UPDATE currency_balances SET balance=@next,revision=revision+1,updated_at=@now WHERE owner_type=@type AND owner_id=@owner AND currency_code=@code AND revision=@revision AND balance=@previous", c, tx))
            {
                money.Parameters.AddWithValue("next", plan.BalanceAfter); money.Parameters.AddWithValue("now", now);
                money.Parameters.AddWithValue("type", walletOwner.Scope.ToString()); money.Parameters.AddWithValue("owner", walletOwner.OwnerId);
                money.Parameters.AddWithValue("code", catalog.CurrencyCode); money.Parameters.AddWithValue("revision", balance.Revision);
                money.Parameters.AddWithValue("previous", balance.Balance);
                if (await money.ExecuteNonQueryAsync(ct) != 1) throw new ShopRuleException("shop_revision_conflict");
            }
            var receipt = new ShopReceipt(command.RequestId, command.Actor, command.ShopId, command.PolicyTag,
                command.Action, command.OfferId, command.ItemId, command.Quantity, command.ExpectedUnitPrice,
                plan.TotalPrice, catalog.CurrencyCode, balance.Balance, plan.BalanceAfter, written, now);
            ShopRules.ValidateReceipt(receipt, command);
            await Save(c, tx, command, fingerprint, JsonSerializer.Serialize(receipt), now, ct);
            await tx.CommitAsync(ct); return receipt;
        }
        catch (ShopRuleException error) when (!assetWriteStarted && ShopRejectionRules.IsDurable(error.Code))
        {
            await using (var rollback = new NpgsqlCommand("ROLLBACK TO SAVEPOINT shop_validation", c, tx))
                await rollback.ExecuteNonQueryAsync(ct);
            var rejection = new ShopRejection(error.Code, await Now(c, tx, ct));
            ShopRejectionRules.Validate(rejection);
            await Save(c, tx, command, fingerprint, JsonSerializer.Serialize(rejection), rejection.RejectedAt, ct);
            await tx.CommitAsync(ct);
            throw new ShopRejectedException(command, rejection.Error, rejection.RejectedAt);
        }
    }

    public async Task<ShopInventorySnapshot> ReadAsync(ShopActor actor, ShopCatalog catalog, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await ReadTransaction(c, ct);
        var context = await Context(c, tx, actor, false, ct);
        if (!catalog.Enabled) throw new ShopRuleException("shop_disabled");
        var wallet = await PostgresEconomyRepository.ReadWalletAsync(c, tx, context, ct);
        var balance = wallet.Balances.SingleOrDefault(b => b.Definition.CurrencyCode == catalog.CurrencyCode);
        if (balance is null || !balance.Definition.Enabled) throw new ShopRuleException("shop_currency_unavailable");
        long held = await PostgresTradeReservationGuards.ReservedCurrency(c, tx, balance.Owner, catalog.CurrencyCode, ct);
        if (held < 0 || held > balance.Balance) throw new ShopRuleException("shop_assets_unavailable");
        var owned = await Owned(c, tx, actor, false, ct);
        var reserved = await PostgresTradeReservationGuards.ReservedItems(c, tx, owned.Select(i => i.ItemId), ct);
        return new(actor, balance, balance.Balance - held, owned, reserved, await Now(c, tx, ct));
    }

    public async Task<ShopReceipt?> FindAsync(ShopActor actor, Guid requestId, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await ReadTransaction(c, ct);
        await Context(c, tx, actor, false, ct);
        var stored = await Stored(c, tx, requestId, actor, false, ct);
        return stored is null ? null : stored.Value.Outcome.Replay(stored.Value.Command);
    }

    public async Task<ShopAssets?> AssetsAsync(ShopActor actor, Guid requestId, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await ReadTransaction(c, ct);
        var context = await Context(c, tx, actor, false, ct);
        var stored = await Stored(c, tx, requestId, actor, false, ct);
        if (stored is null) return null;
        _ = stored.Value.Outcome.Replay(stored.Value.Command);
        return new(requestId, actor.CharacterId, await Now(c, tx, ct),
            await PostgresEconomyRepository.ReadWalletAsync(c, tx, context, ct), await Owned(c, tx, actor, false, ct));
    }

    private static async Task<NpgsqlTransaction> ReadTransaction(NpgsqlConnection c, CancellationToken ct)
    {
        var tx = await c.BeginTransactionAsync(IsolationLevel.RepeatableRead, ct);
        try { await using var mode = new NpgsqlCommand("SET TRANSACTION READ ONLY", c, tx); await mode.ExecuteNonQueryAsync(ct); return tx; }
        catch { await tx.DisposeAsync(); throw; }
    }
    private static async Task<CharacterEconomyContext> Context(NpgsqlConnection c, NpgsqlTransaction tx, ShopActor actor, bool write, CancellationToken ct)
    {
        if (!ShopRules.ValidActor(actor)) throw new ShopRuleException("invalid_shop_actor");
        await using var q = new NpgsqlCommand("SELECT roster_id FROM characters WHERE character_id=@id AND steam_id=@account" + (write ? " FOR UPDATE" : ""), c, tx);
        q.Parameters.AddWithValue("id", actor.CharacterId); q.Parameters.AddWithValue("account", actor.AccountId);
        if (await q.ExecuteScalarAsync(ct) is not Guid roster) throw new ShopRuleException("shop_character_not_owned");
        return new(actor.CharacterId, roster, actor.AccountId);
    }
    private static async Task<IReadOnlyList<ItemRecord>> Owned(NpgsqlConnection c, NpgsqlTransaction tx, ShopActor actor, bool write, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand($"SELECT {PostgresItemRepository.ItemColumns} FROM item_records WHERE owner_type='Character' AND owner_id=@owner AND lifecycle_state='Active' ORDER BY item_id LIMIT 501" + (write ? " FOR UPDATE" : ""), c, tx);
        q.Parameters.AddWithValue("owner", actor.CharacterId.ToString("D"));
        List<ItemRecord> records = [];
        await using var r = await q.ExecuteReaderAsync(ct);
        while (await r.ReadAsync(ct)) records.Add(PostgresItemRepository.ReadItemRecord(r));
        if (records.Count > 500) throw new ShopRuleException("shop_assets_unavailable");
        return records;
    }
    private static async Task<(string Fingerprint, ShopCommand Command, ShopStoredOutcome Outcome)?> Stored(
        NpgsqlConnection c, NpgsqlTransaction tx, Guid id, ShopActor actor, bool forReplay, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT account_id,character_id,fingerprint,command::text,result::text,committed_at FROM shop_receipts WHERE request_id=@id", c, tx);
        q.Parameters.AddWithValue("id", id);
        await using var r = await q.ExecuteReaderAsync(ct);
        if (!await r.ReadAsync(ct)) return null;
        var storedActor = new ShopActor(r.GetString(0), r.GetGuid(1));
        if (storedActor != actor)
        {
            if (forReplay) throw new ShopRuleException("shop_idempotency_conflict");
            return null;
        }
        var command = JsonSerializer.Deserialize<ShopCommand>(r.GetString(3)) ?? throw new InvalidDataException("Missing shop command.");
        if (command.RequestId != id || command.Actor != storedActor || ShopRules.Fingerprint(command) != r.GetString(2))
            throw new InvalidDataException("Shop receipt index mismatch.");
        var outcome = ShopStoredOutcome.Decode(r.GetString(4), command);
        if (outcome.At != r.GetFieldValue<DateTimeOffset>(5)) throw new InvalidDataException("Shop outcome timestamp mismatch.");
        return (r.GetString(2), command, outcome);
    }

    private static async Task Save(NpgsqlConnection c, NpgsqlTransaction tx, ShopCommand command,
        string fingerprint, string payload, DateTimeOffset now, CancellationToken ct)
    {
        await using var insert = new NpgsqlCommand("INSERT INTO shop_receipts(request_id,account_id,character_id,fingerprint,command,result,committed_at) VALUES(@id,@account,@character,@fingerprint,@command,@result,@now)", c, tx);
        insert.Parameters.AddWithValue("id", command.RequestId); insert.Parameters.AddWithValue("account", command.Actor.AccountId);
        insert.Parameters.AddWithValue("character", command.Actor.CharacterId); insert.Parameters.AddWithValue("fingerprint", fingerprint);
        insert.Parameters.AddWithValue("command", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(command));
        insert.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, payload); insert.Parameters.AddWithValue("now", now);
        await insert.ExecuteNonQueryAsync(ct);
    }
    private static async Task<ItemRecord[]> Write(NpgsqlConnection c, NpgsqlTransaction tx, IReadOnlyList<ItemRecordMutation> changes, DateTimeOffset now, CancellationToken ct)
    {
        if (changes.Count is < 1 or > InventoryBagLayout.TotalSlots + 1
            || changes.Select(m => m.NewRecord.ItemId).Distinct().Count() != changes.Count
            || changes.Any(m => m.ExpectedRevision < 0 || m.ExpectedRevision == long.MaxValue
                || m.NewRecord.Revision != m.ExpectedRevision || !ItemRecordRules.TryValidateRecord(m.NewRecord, out _)))
            throw new ShopRuleException("invalid_shop_mutation");
        foreach (var change in changes.Where(m => m.ExpectedRevision > 0))
        {
            var old = await PostgresItemRepository.ReadItemForUpdateAsync(c, tx, change.NewRecord.ItemId, ct);
            if (old?.Revision != change.ExpectedRevision) throw new ShopRuleException("shop_revision_conflict");
        }
        foreach (var change in changes.Where(m => m.ExpectedRevision > 0))
            await PostgresItemRepository.StageExistingRecordAsync(c, tx, change.NewRecord.ItemId, ct);
        var written = changes.Select(m => ItemRecordRules.SnapshotWithRevision(m.NewRecord, m.ExpectedRevision + 1)).ToArray();
        foreach (var item in written)
            await PostgresItemRepository.WriteRecordAsync(c, tx, item, changes.Single(m => m.NewRecord.ItemId == item.ItemId).ExpectedRevision == 0, now, ct);
        return written;
    }
}
