using Npgsql;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

internal static class PostgresTradeReservationGuards
{
    // Caller holds the same item-owner locks as reservation creation. Never trust a command's timestamp here.
    internal static async Task<HashSet<Guid>> ReservedItems(NpgsqlConnection connection, NpgsqlTransaction transaction,
        IEnumerable<Guid> itemIds, CancellationToken ct)
    {
        Guid[] ids = itemIds.Distinct().ToArray();
        if (ids.Length == 0) return [];
        await using NpgsqlCommand query = new("""
            SELECT DISTINCT i.item_id FROM trade_reserved_items i
            JOIN trade_asset_reservations r ON r.reservation_id=i.reservation_id
            WHERE i.item_id=ANY(@ids) AND r.state='Active' AND r.expires_at>clock_timestamp()
            UNION
            SELECT item_id FROM item_records WHERE item_id=ANY(@ids)
              AND (container_type='Auction' OR (owner_type='System' AND lower(owner_id) LIKE 'auction:%'));
            """, connection, transaction);
        query.Parameters.AddWithValue("ids", ids);
        HashSet<Guid> result = [];
        await using var reader = await query.ExecuteReaderAsync(ct);
        while (await reader.ReadAsync(ct)) result.Add(reader.GetGuid(0));
        return result;
    }

    // Caller holds currency_balances FOR UPDATE; this covers shared account/roster wallets too.
    internal static async Task<long> ReservedCurrency(NpgsqlConnection connection, NpgsqlTransaction transaction,
        CurrencyOwnerRef owner, string currencyCode, CancellationToken ct)
    {
        await using NpgsqlCommand query = new("""
            SELECT COALESCE(sum(currency_amount),0)::bigint FROM trade_asset_reservations
            WHERE wallet_owner_type=@type AND wallet_owner_id=@owner AND currency_code=@code
              AND state='Active' AND expires_at>clock_timestamp();
            """, connection, transaction);
        query.Parameters.AddWithValue("type", owner.Scope.ToString());
        query.Parameters.AddWithValue("owner", owner.OwnerId);
        query.Parameters.AddWithValue("code", currencyCode);
        return (long)(await query.ExecuteScalarAsync(ct))!;
    }
}
