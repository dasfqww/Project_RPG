using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed class PostgresTradeReservationRepository(NpgsqlDataSource dataSource, TradeItemPolicy policy)
    : ITradeReservationRepository
{
    public async Task<TradeAssetReservation> ReserveAsync(TradeReservationCommand command, CancellationToken ct)
    {
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var transaction = await connection.BeginTransactionAsync(ct);
        var result = await ReserveInTransactionAsync(connection, transaction, command, ct);
        await transaction.CommitAsync(ct);
        return result;
    }

    internal async Task<TradeAssetReservation> ReserveInTransactionAsync(NpgsqlConnection connection, NpgsqlTransaction transaction, TradeReservationCommand command, CancellationToken ct)
    {
        command = TradeReservationRules.Validate(command);
        string fingerprint = TradeReservationRules.Fingerprint(command);
        var context = await Context(connection, transaction, command.Participant, ct);
        await LockReservation(connection, transaction, command.ReservationId, ct);
        var existing = await Read(connection, transaction, command.ReservationId, ct);
        if (existing is not null) return TradeReservationRules.Replay(existing, fingerprint, await Now(connection, transaction, ct));

        // Reward settlement takes item-owner locks BEFORE wallet locks. Match that order.
        ItemOwnerRef itemOwner = new(ItemOwnerType.Character, context.CharacterId.ToString("D"));
        await PostgresItemRepository.AcquireOwnerLocksAsync(connection, transaction, [itemOwner], ct);
        var definition = await PostgresEconomyRepository.ReadDefinitionAsync(connection, transaction, command.CurrencyCode, ct);
        if (definition is null || !definition.Enabled) throw new TradeRuleException("reservation_currency_unavailable");
        CurrencyOwnerRef owner = new(definition.Scope, EconomyRules.ResolveOwnerId(definition.Scope, context));
        await PostgresEconomyRepository.EnsureBalanceRowAsync(connection, transaction, owner, command.CurrencyCode,
            await Now(connection, transaction, ct), ct);
        var balance = await PostgresEconomyRepository.LockBalanceAsync(connection, transaction, owner, command.CurrencyCode, ct);
        if (command.Offer.CurrencyAmount > balance.Balance - await PostgresTradeReservationGuards.ReservedCurrency(
                connection, transaction, owner, command.CurrencyCode, ct))
            throw new TradeRuleException("reservation_insufficient_balance");

        await using (NpgsqlCommand active = new("""
            SELECT EXISTS(SELECT 1 FROM trade_asset_reservations
                WHERE character_id=@id AND state='Active' AND expires_at>clock_timestamp());
            """, connection, transaction))
        {
            active.Parameters.AddWithValue("id", context.CharacterId);
            if ((bool)(await active.ExecuteScalarAsync(ct))!) throw new TradeRuleException("character_already_reserved");
        }
        // Resolve ownership before taking an item row lock, to avoid locking arbitrary foreign items.
        foreach (var offered in command.Offer.Items)
        {
            await using NpgsqlCommand ownership = new("SELECT EXISTS(SELECT 1 FROM item_records WHERE item_id=@id AND owner_type='Character' AND owner_id=@owner);", connection, transaction);
            ownership.Parameters.AddWithValue("id", offered.ItemId);
            ownership.Parameters.AddWithValue("owner", itemOwner.OwnerId);
            if (!(bool)(await ownership.ExecuteScalarAsync(ct))!) throw new TradeRuleException("reservation_item_not_found");
            var item = await PostgresItemRepository.ReadItemForUpdateAsync(connection, transaction, offered.ItemId, ct)
                ?? throw new TradeRuleException("reservation_item_not_found");
            policy.ValidateOfferItem(item, command.Participant, offered, TradeChannel.Direct, await Now(connection, transaction, ct));
        }
        if ((await PostgresTradeReservationGuards.ReservedItems(connection, transaction, command.Offer.Items.Select(item => item.ItemId), ct)).Count != 0)
            throw new TradeRuleException("reservation_item_busy");
        if (command.ExpiresAt <= await Now(connection, transaction, ct)) throw new TradeRuleException("reservation_expired");
        TradeAssetReservation result = new(command, fingerprint, owner, TradeReservationStatus.Active);
        await Insert(connection, transaction, result, ct);
        foreach (var offered in command.Offer.Items)
        {
            await using NpgsqlCommand insert = new("INSERT INTO trade_reserved_items(reservation_id,item_id) VALUES(@reservation,@item);", connection, transaction);
            insert.Parameters.AddWithValue("reservation", command.ReservationId);
            insert.Parameters.AddWithValue("item", offered.ItemId);
            await insert.ExecuteNonQueryAsync(ct);
        }
        return result;
    }

    public async Task<TradeAssetReservation> ReleaseAsync(TradeReservationCommand command, CancellationToken ct)
    {
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        await using var transaction = await connection.BeginTransactionAsync(ct);
        var result = await ReleaseInTransactionAsync(connection, transaction, command, ct);
        await transaction.CommitAsync(ct);
        return result;
    }

    internal async Task<TradeAssetReservation> ReleaseInTransactionAsync(NpgsqlConnection connection, NpgsqlTransaction transaction, TradeReservationCommand command, CancellationToken ct)
    {
        command = TradeReservationRules.Validate(command);
        string fingerprint = TradeReservationRules.Fingerprint(command);
        await Context(connection, transaction, command.Participant, ct);
        await LockReservation(connection, transaction, command.ReservationId, ct);
        var existing = await Read(connection, transaction, command.ReservationId, ct);
        TradeAssetReservation result;
        if (existing is not null)
        {
            result = TradeReservationRules.Replay(existing, fingerprint, await Now(connection, transaction, ct)) with { Status = TradeReservationStatus.Released };
            await using NpgsqlCommand update = new("UPDATE trade_asset_reservations SET state='Released' WHERE reservation_id=@id;", connection, transaction);
            update.Parameters.AddWithValue("id", command.ReservationId);
            await update.ExecuteNonQueryAsync(ct);
        }
        else
        {
            result = new(command, fingerprint, null, TradeReservationStatus.Released);
            await Insert(connection, transaction, result, ct);
        }
        return result;
    }

    public async Task<TradeAssetReservation?> FindAsync(Guid id, TradeParticipant participant, CancellationToken ct)
    {
        await using var connection = await dataSource.OpenConnectionAsync(ct);
        var result = await Read(connection, null, id, ct);
        return result?.Command.Participant == participant ? result.At(await Now(connection, null, ct)) : null;
    }

    internal static async Task<CharacterEconomyContext> Context(NpgsqlConnection connection, NpgsqlTransaction transaction,
        TradeParticipant participant, CancellationToken ct)
    {
        // Acquire before wallet/item locks: later FK checks must not invert equipment's character -> item order.
        await using NpgsqlCommand query = new("SELECT roster_id FROM characters WHERE character_id=@id AND steam_id=@account FOR KEY SHARE;", connection, transaction);
        query.Parameters.AddWithValue("id", participant.CharacterId);
        query.Parameters.AddWithValue("account", participant.AccountId);
        if (await query.ExecuteScalarAsync(ct) is not Guid roster) throw new TradeRuleException("character_not_owned");
        return new(participant.CharacterId, roster, participant.AccountId);
    }

    internal static async Task LockReservation(NpgsqlConnection connection, NpgsqlTransaction transaction, Guid id, CancellationToken ct)
    {
        long key = BinaryPrimitives.ReadInt64LittleEndian(SHA256.HashData(Encoding.UTF8.GetBytes("trade_reservation|" + id.ToString("D"))));
        await using NpgsqlCommand query = new("SELECT pg_advisory_xact_lock(@key);", connection, transaction);
        query.Parameters.AddWithValue("key", key);
        await query.ExecuteNonQueryAsync(ct);
    }

    internal static async Task<DateTimeOffset> Now(NpgsqlConnection connection, NpgsqlTransaction? transaction, CancellationToken ct)
    {
        await using NpgsqlCommand query = new("SELECT clock_timestamp();", connection, transaction);
        return new DateTimeOffset((DateTime)(await query.ExecuteScalarAsync(ct))!);
    }

    internal static async Task<TradeAssetReservation?> Read(NpgsqlConnection connection, NpgsqlTransaction? transaction, Guid id, CancellationToken ct)
    {
        await using NpgsqlCommand query = new("""
            SELECT command_payload::text, fingerprint, wallet_owner_type, wallet_owner_id, state
            FROM trade_asset_reservations WHERE reservation_id=@id;
            """, connection, transaction);
        query.Parameters.AddWithValue("id", id);
        await using var reader = await query.ExecuteReaderAsync(ct);
        if (!await reader.ReadAsync(ct)) return null;
        var command = TradeReservationRules.Deserialize(reader.GetString(0));
        string fingerprint = reader.GetString(1);
        if (command.ReservationId != id || TradeReservationRules.Fingerprint(command) != fingerprint)
            throw new InvalidDataException("Reservation receipt integrity failed.");
        CurrencyOwnerRef? wallet = reader.IsDBNull(2) ? null : new(Enum.Parse<CurrencyScope>(reader.GetString(2)), reader.GetString(3));
        return new(command, fingerprint, wallet, Enum.Parse<TradeReservationStatus>(reader.GetString(4)));
    }

    private static async Task Insert(NpgsqlConnection connection, NpgsqlTransaction transaction, TradeAssetReservation value, CancellationToken ct)
    {
        await using NpgsqlCommand insert = new("""
            INSERT INTO trade_asset_reservations(reservation_id,trade_id,character_id,account_id,fingerprint,command_payload,
                state,expires_at,wallet_owner_type,wallet_owner_id,currency_code,currency_amount)
            VALUES(@id,@trade,@character,@account,@fingerprint,@payload,@state,@expires,@type,@owner,@code,@amount);
            """, connection, transaction);
        var command = value.Command;
        insert.Parameters.AddWithValue("id", command.ReservationId);
        insert.Parameters.AddWithValue("trade", command.TradeId);
        insert.Parameters.AddWithValue("character", command.Participant.CharacterId);
        insert.Parameters.AddWithValue("account", command.Participant.AccountId);
        insert.Parameters.AddWithValue("fingerprint", value.Fingerprint);
        insert.Parameters.AddWithValue("payload", NpgsqlDbType.Jsonb, TradeReservationRules.Serialize(command));
        insert.Parameters.AddWithValue("state", value.Status.ToString());
        insert.Parameters.AddWithValue("expires", command.ExpiresAt);
        insert.Parameters.AddWithValue("type", NpgsqlDbType.Varchar, (object?)value.WalletOwner?.Scope.ToString() ?? DBNull.Value);
        insert.Parameters.AddWithValue("owner", NpgsqlDbType.Varchar, (object?)value.WalletOwner?.OwnerId ?? DBNull.Value);
        insert.Parameters.AddWithValue("code", command.CurrencyCode);
        insert.Parameters.AddWithValue("amount", command.Offer.CurrencyAmount);
        await insert.ExecuteNonQueryAsync(ct);
    }
}
