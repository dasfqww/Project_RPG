using System.Text.Json;
using Npgsql;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresAuctionQueryRepository(NpgsqlDataSource dataSource) : IAuctionQueryRepository
{
    public async Task<AuctionPage> SearchAsync(AuctionSearch q, string? sellerAccount, CancellationToken ct)
    {
        AuctionQueryRules.Validate(q);
        var found = await Select(q, sellerAccount, false, ct);
        return AuctionQueryRules.Page(found, q.Limit);
    }
    public async Task<IReadOnlyList<AuctionEscrowListing>> FindExpiredAsync(int limit, AuctionCursor? after, CancellationToken ct)
    {
        var q = new AuctionSearch(limit, After: after); AuctionQueryRules.Validate(q);
        return (await Select(q, null, true, ct)).Take(limit).ToArray();
    }
    private async Task<List<AuctionEscrowListing>> Select(AuctionSearch q, string? account, bool expired, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        string where = account is not null ? "seller_account=@account" : expired ? "NOT closed AND expires_at<=statement_timestamp()" : "NOT closed AND expires_at>statement_timestamp()";
        if (q.DefinitionName is not null) where += " AND payload->'EscrowAtRegistration'->>'DefinitionName'=@definition";
        if (q.CurrencyCode is not null) where += " AND payload->'State'->'Terms'->>'CurrencyCode'=@currency";
        if (q.MinimumPrice is not null) where += " AND (payload->'State'->>'TotalPrice')::bigint>=@min";
        if (q.MaximumPrice is not null) where += " AND (payload->'State'->>'TotalPrice')::bigint<=@max";
        if (q.After is not null) where += " AND (expires_at,listing_id)>(@after,@id)";
        await using var cmd = new NpgsqlCommand("SELECT payload::text,listing_id,seller_account,seller_character,revision,closed,expires_at FROM auction_escrow_listings WHERE " + where + " ORDER BY expires_at,listing_id LIMIT @limit", c);
        cmd.Parameters.AddWithValue("limit", q.Limit + 1);
        if (account is not null) cmd.Parameters.AddWithValue("account", account);
        if (q.DefinitionName is not null) cmd.Parameters.AddWithValue("definition", q.DefinitionName);
        if (q.CurrencyCode is not null) cmd.Parameters.AddWithValue("currency", q.CurrencyCode);
        if (q.MinimumPrice is { } min) cmd.Parameters.AddWithValue("min", min);
        if (q.MaximumPrice is { } max) cmd.Parameters.AddWithValue("max", max);
        if (q.After is { } after) { cmd.Parameters.AddWithValue("after", after.ExpiresAt); cmd.Parameters.AddWithValue("id", after.ListingId); }
        await using var r = await cmd.ExecuteReaderAsync(ct); List<AuctionEscrowListing> found = [];
        while (await r.ReadAsync(ct)) found.Add(Decode(r));
        return found;
    }
    public async Task<AuctionEscrowListing?> FindVisibleAsync(Guid id, string account, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var cmd = new NpgsqlCommand("SELECT payload::text,listing_id,seller_account,seller_character,revision,closed,expires_at FROM auction_escrow_listings WHERE listing_id=@id AND (seller_account=@account OR (NOT closed AND expires_at>statement_timestamp()))", c);
        cmd.Parameters.AddWithValue("id", id); cmd.Parameters.AddWithValue("account", account);
        await using var r = await cmd.ExecuteReaderAsync(ct); return await r.ReadAsync(ct) ? Decode(r) : null;
    }
    private static AuctionEscrowListing Decode(NpgsqlDataReader r)
    {
        var l = JsonSerializer.Deserialize<AuctionEscrowListing>(r.GetString(0)) ?? throw new InvalidDataException("Missing auction listing.");
        AuctionEscrowRules.Validate(l);
        if (l.State.ListingId != r.GetGuid(1) || l.State.Seller != new TradeParticipant(r.GetString(2), r.GetGuid(3)) || l.State.Revision != r.GetInt64(4)
            || (l.State.Status != AuctionListingStatus.Active) != r.GetBoolean(5) || AuctionQueryRules.Cursor(l).ExpiresAt != r.GetFieldValue<DateTimeOffset>(6))
            throw new InvalidDataException("Auction query index mismatch.");
        return l;
    }
    public async Task<AuctionStoredReceipt?> FindReceiptAsync(Guid id, TradeParticipant actor, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        return await ReadReceipt(c, null, id, actor, ct);
    }
    private static async Task<AuctionStoredReceipt?> ReadReceipt(NpgsqlConnection c, NpgsqlTransaction? tx, Guid id, TradeParticipant actor, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT 'escrow',result::text FROM auction_escrow_receipts WHERE request_id=@id AND actor_account=@account AND actor_character=@character UNION ALL SELECT 'purchase',result::text FROM auction_sales WHERE request_id=@id AND buyer_account=@account AND buyer_character=@character UNION ALL SELECT 'claim',result::text FROM auction_claim_receipts WHERE request_id=@id AND actor_account=@account AND actor_character=@character", c, tx);
        q.Parameters.AddWithValue("id", id); q.Parameters.AddWithValue("account", actor.AccountId); q.Parameters.AddWithValue("character", actor.CharacterId);
        await using var r = await q.ExecuteReaderAsync(ct); if (!await r.ReadAsync(ct)) return null;
        var receipt = new AuctionStoredReceipt(r.GetString(0), r.GetString(1));
        if (AuctionQueryRules.ReceiptIdentity(receipt) != (id, actor) || await r.ReadAsync(ct)) throw new InvalidDataException("Auction receipt scope mismatch.");
        return receipt;
    }
}
