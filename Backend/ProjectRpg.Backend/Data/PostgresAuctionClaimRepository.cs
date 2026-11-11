using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

// Internal persistence boundary. Authentication/application/API wiring is deliberately separate.
public sealed class PostgresAuctionClaimRepository(NpgsqlDataSource dataSource) : IAuctionClaimRepository
{
    public async Task<AuctionSellerPayable?> FindPayableAsync(Guid listingId, TradeParticipant claimant, CancellationToken ct)
    {
        if (listingId == Guid.Empty || !AuctionTerms.ValidParticipant(claimant)) throw new TradeRuleException("invalid_auction_claim");
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        var context = await Context(c, tx, claimant, ct);
        var p = await Read(c, tx, listingId, ct);
        return p is not null && Authorized(p, claimant, context) ? p : null;
    }

    public async Task<AuctionClaimResult> ClaimAsync(AuctionClaimCommand cmd, CancellationToken ct)
    {
        AuctionClaimRules.Validate(cmd);
        var fingerprint = AuctionClaimRules.Fingerprint(cmd);
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        await PostgresServiceAccess.LockAsync(c, tx, false, ct);
        await PostgresAccountAccess.LockAsync(c, tx, cmd.Claimant.AccountId, ct);
        await Gate(c, tx, "request", cmd.RequestId, ct);
        var receipt = await Receipt(c, tx, cmd, fingerprint, ct);
        if (receipt is not null) return receipt;
        await Gate(c, tx, "listing", cmd.ListingId, ct);
        var context = await Context(c, tx, cmd.Claimant, ct);
        var p = await Read(c, tx, cmd.ListingId, ct) ?? throw new TradeRuleException("auction_payable_not_found");
        if (!Authorized(p, cmd.Claimant, context)) throw new TradeRuleException("auction_payable_owner_required");
        if (p.Revision != cmd.ExpectedRevision) throw new TradeRuleException("auction_payable_revision_conflict");
        if (p.CurrencyCode != cmd.ExpectedCurrencyCode) throw new TradeRuleException("auction_currency_conflict");
        await PostgresServiceAccess.EnsureOpenAsync(c, tx, ct);
        await PostgresAccountAccess.EnsureAsync(c, tx, cmd.Claimant.AccountId, await Now(c, tx, ct), ct);
        await using (var lease = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM character_session_leases WHERE character_id=@id AND expires_at>clock_timestamp())", c, tx))
        {
            lease.Parameters.AddWithValue("id", cmd.Claimant.CharacterId);
            if ((bool)(await lease.ExecuteScalarAsync(ct))!) throw new TradeRuleException("auction_requires_lobby");
        }
        var definition = await PostgresEconomyRepository.ReadDefinitionAsync(c, tx, p.CurrencyCode, ct);
        if (definition is null || !definition.Enabled || !EconomyRules.TryValidateDefinition(definition, out _))
            throw new TradeRuleException("auction_currency_unavailable");
        if (definition.Scope != p.Wallet.Scope) throw new TradeRuleException("auction_payable_owner_required");
        await PostgresEconomyRepository.EnsureBalanceRowAsync(c, tx, p.Wallet, p.CurrencyCode, await Now(c, tx, ct), ct);
        var balance = await PostgresEconomyRepository.LockBalanceAsync(c, tx, p.Wallet, p.CurrencyCode, ct);
        if (balance.Balance < 0 || balance.Balance > definition.MaxBalance) throw new TradeRuleException("auction_wallet_limit");
        var amounts = AuctionMoney.Claim(p.RemainingAmount, cmd.RequestedAmount, balance.Balance, definition.MaxBalance);
        var after = p with { RemainingAmount = amounts.RemainingPayable, Revision = p.Revision + (amounts.Paid > 0 ? 1 : 0) };
        if (amounts.Paid > 0)
        {
            if (balance.Revision == long.MaxValue) throw new TradeRuleException("auction_wallet_revision_exhausted");
            await using var debit = new NpgsqlCommand("UPDATE auction_seller_payables SET remaining_amount=@next,revision=revision+1 WHERE listing_id=@id AND revision=@revision AND remaining_amount=@previous", c, tx);
            debit.Parameters.AddWithValue("next", after.RemainingAmount); debit.Parameters.AddWithValue("id", p.ListingId);
            debit.Parameters.AddWithValue("revision", p.Revision); debit.Parameters.AddWithValue("previous", p.RemainingAmount);
            if (await debit.ExecuteNonQueryAsync(ct) != 1) throw new TradeRuleException("auction_payable_revision_conflict");
            await using var credit = new NpgsqlCommand("UPDATE currency_balances SET balance=@next,revision=revision+1,updated_at=clock_timestamp() WHERE owner_type=@type AND owner_id=@owner AND currency_code=@code AND revision=@revision AND balance=@previous", c, tx);
            credit.Parameters.AddWithValue("next", amounts.NewWalletBalance); credit.Parameters.AddWithValue("type", p.Wallet.Scope.ToString());
            credit.Parameters.AddWithValue("owner", p.Wallet.OwnerId); credit.Parameters.AddWithValue("code", p.CurrencyCode);
            credit.Parameters.AddWithValue("revision", balance.Revision); credit.Parameters.AddWithValue("previous", balance.Balance);
            if (await credit.ExecuteNonQueryAsync(ct) != 1) throw new TradeRuleException("auction_wallet_revision_conflict");
        }
        var result = new AuctionClaimResult(cmd.RequestId, cmd.Claimant, p, after,
            new(definition, balance.Balance, amounts.NewWalletBalance, balance.Revision), amounts.Paid, await Now(c, tx, ct));
        AuctionClaimRules.Validate(result, cmd);
        await using (var insert = new NpgsqlCommand("INSERT INTO auction_claim_receipts(request_id,listing_id,actor_account,actor_character,fingerprint,paid,result,committed_at) VALUES(@id,@listing,@account,@character,@fingerprint,@paid,@result,@now)", c, tx))
        {
            insert.Parameters.AddWithValue("id", cmd.RequestId); insert.Parameters.AddWithValue("listing", cmd.ListingId);
            insert.Parameters.AddWithValue("account", cmd.Claimant.AccountId); insert.Parameters.AddWithValue("character", cmd.Claimant.CharacterId);
            insert.Parameters.AddWithValue("fingerprint", fingerprint); insert.Parameters.AddWithValue("paid", result.Paid);
            insert.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(result)); insert.Parameters.AddWithValue("now", result.CommittedAt);
            await insert.ExecuteNonQueryAsync(ct);
        }
        await tx.CommitAsync(ct);
        return result;
    }

    private static bool Authorized(AuctionSellerPayable p, TradeParticipant actor, CharacterEconomyContext context) =>
        p.Seller.AccountId == actor.AccountId && p.Wallet.OwnerId == EconomyRules.ResolveOwnerId(p.Wallet.Scope, context);

    private static async Task Gate(NpgsqlConnection c, NpgsqlTransaction tx, string kind, Guid id, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", c, tx);
        q.Parameters.AddWithValue("key", "auction_" + kind + ":" + id.ToString("D")); await q.ExecuteNonQueryAsync(ct);
    }

    private static async Task<AuctionSellerPayable?> Read(NpgsqlConnection c, NpgsqlTransaction tx, Guid id, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT p.seller_account,p.seller_character,p.currency_code,p.wallet_owner_type,p.wallet_owner_id,p.initial_amount,p.remaining_amount,p.revision,s.result::text FROM auction_seller_payables p JOIN auction_sales s USING(listing_id) WHERE p.listing_id=@id", c, tx);
        q.Parameters.AddWithValue("id", id);
        await using var r = await q.ExecuteReaderAsync(ct);
        if (!await r.ReadAsync(ct)) return null;
        var p = new AuctionSellerPayable(id, new(r.GetString(0), r.GetGuid(1)), new(Enum.Parse<CurrencyScope>(r.GetString(3)), r.GetString(4)), r.GetString(2), r.GetInt64(5), r.GetInt64(6), r.GetInt64(7));
        AuctionClaimRules.Validate(p);
        var original = JsonSerializer.Deserialize<AuctionPurchaseResult>(r.GetString(8)) ?? throw new InvalidDataException("Missing sale proof.");
        var s = original.Listing.State;
        AuctionPurchaseRules.Validate(original, new(original.RequestId, id, original.Listing.Sale!.Buyer, s.Revision - 1, s.Terms.CurrencyCode, s.TotalPrice));
        if (p.Seller != s.Seller || p.CurrencyCode != s.Terms.CurrencyCode || p.Wallet != original.Listing.Sale.SellerWallet || p.InitialAmount != original.Listing.Sale.Amounts.SellerPayable)
            throw new InvalidDataException("Payable does not match original sale.");
        return p;
    }

    private static async Task<AuctionClaimResult?> Receipt(NpgsqlConnection c, NpgsqlTransaction tx, AuctionClaimCommand cmd, string fingerprint, CancellationToken ct)
    {
        await using (var q = new NpgsqlCommand("SELECT fingerprint,result::text,listing_id,actor_account,actor_character,paid,committed_at FROM auction_claim_receipts WHERE request_id=@id", c, tx))
        {
            q.Parameters.AddWithValue("id", cmd.RequestId);
            await using var r = await q.ExecuteReaderAsync(ct);
            if (await r.ReadAsync(ct))
            {
                if (r.GetString(0) != fingerprint) throw new TradeRuleException("auction_idempotency_conflict");
                var result = JsonSerializer.Deserialize<AuctionClaimResult>(r.GetString(1)) ?? throw new InvalidDataException("Missing claim receipt.");
                AuctionClaimRules.Validate(result, cmd);
                if (result.Payable.ListingId != r.GetGuid(2) || result.Claimant != new TradeParticipant(r.GetString(3), r.GetGuid(4)) || result.Paid != r.GetInt64(5) || result.CommittedAt != r.GetFieldValue<DateTimeOffset>(6))
                    throw new InvalidDataException("Claim index mismatch.");
                return result;
            }
        }
        await using var other = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM auction_sales WHERE request_id=@id UNION ALL SELECT 1 FROM auction_escrow_receipts WHERE request_id=@id)", c, tx);
        other.Parameters.AddWithValue("id", cmd.RequestId);
        if ((bool)(await other.ExecuteScalarAsync(ct))!) throw new TradeRuleException("auction_idempotency_conflict");
        return null;
    }
}
