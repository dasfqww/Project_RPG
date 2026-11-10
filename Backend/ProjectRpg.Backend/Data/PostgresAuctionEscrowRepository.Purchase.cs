using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresAuctionEscrowRepository
{
    public async Task<AuctionPurchaseResult> PurchaseAsync(AuctionPurchaseCommand command, CancellationToken ct)
    {
        AuctionPurchaseRules.Validate(command);
        string fingerprint = AuctionPurchaseRules.Fingerprint(command);
        await using var c = await dataSource.OpenConnectionAsync(ct);
        // Seller identity is immutable; reread authoritative state under the shared listing gate.
        var initial = await Load(c, null, command.ListingId, ct) ?? throw new TradeRuleException("auction_not_found");
        var seller = initial.State.Seller;
        await using var tx = await c.BeginTransactionAsync(ct);
        await PostgresServiceAccess.LockAsync(c, tx, false, ct);
        foreach (var account in new[] { seller.AccountId, command.Buyer.AccountId }.Distinct().Order(StringComparer.Ordinal))
            await PostgresAccountAccess.LockAsync(c, tx, account, ct);
        await Gate(c, tx, "request", command.RequestId, ct);
        var receipt = await PurchaseReceipt(c, tx, command, fingerprint, ct);
        if (receipt is not null) return receipt;
        await Gate(c, tx, "listing", command.ListingId, ct);
        var listing = await Load(c, tx, command.ListingId, ct) ?? throw new TradeRuleException("auction_not_found");
        if (listing.State.Seller != seller) throw new InvalidDataException("Auction seller changed under gate.");
        var now = await Now(c, tx, ct);
        listing.State.AgreePurchase(command.Buyer, command.ExpectedRevision, command.ExpectedCurrencyCode, command.ExpectedTotalPrice, now);
        Dictionary<TradeParticipant, CharacterEconomyContext> contexts = [];
        foreach (var p in new[] { seller, command.Buyer }.OrderBy(p => p.CharacterId)) contexts[p] = await Context(c, tx, p, ct);
        await PostgresServiceAccess.EnsureOpenAsync(c, tx, ct);
        foreach (var account in new[] { seller.AccountId, command.Buyer.AccountId }.Order(StringComparer.Ordinal))
            await PostgresAccountAccess.EnsureAsync(c, tx, account, await Now(c, tx, ct), ct);
        // Offline/in-dungeon sellers are allowed: only the buyer must be in the lobby.
        await using (var lease = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM character_session_leases WHERE character_id=@id AND expires_at>clock_timestamp())", c, tx))
        {
            lease.Parameters.AddWithValue("id", command.Buyer.CharacterId);
            if ((bool)(await lease.ExecuteScalarAsync(ct))!) throw new TradeRuleException("auction_requires_lobby");
        }
        var buyerOwner = new ItemOwnerRef(ItemOwnerType.Character, command.Buyer.CharacterId.ToString("D"));
        await PostgresItemRepository.AcquireOwnerLocksAsync(c, tx,
            [new(ItemOwnerType.Character, seller.CharacterId.ToString("D")), buyerOwner, AuctionEscrowRules.Owner(command.ListingId)], ct);
        var definition = await PostgresEconomyRepository.ReadDefinitionAsync(c, tx, command.ExpectedCurrencyCode, ct);
        if (definition is null || !definition.Enabled || !EconomyRules.TryValidateDefinition(definition, out _))
            throw new TradeRuleException("auction_currency_unavailable");
        var buyerWallet = new CurrencyOwnerRef(definition.Scope, EconomyRules.ResolveOwnerId(definition.Scope, contexts[command.Buyer]));
        var sellerWallet = new CurrencyOwnerRef(definition.Scope, EconomyRules.ResolveOwnerId(definition.Scope, contexts[seller]));
        if (buyerWallet == sellerWallet) throw new TradeRuleException("auction_self_purchase");
        await PostgresEconomyRepository.EnsureBalanceRowAsync(c, tx, buyerWallet, definition.CurrencyCode, await Now(c, tx, ct), ct);
        var balance = await PostgresEconomyRepository.LockBalanceAsync(c, tx, buyerWallet, definition.CurrencyCode, ct);
        long reserved = await PostgresTradeReservationGuards.ReservedCurrency(c, tx, buyerWallet, definition.CurrencyCode, ct);
        if (reserved < 0 || reserved > balance.Balance || command.ExpectedTotalPrice > balance.Balance - reserved)
            throw new TradeRuleException("auction_insufficient_balance");
        if (balance.Revision == long.MaxValue) throw new TradeRuleException("auction_wallet_revision_exhausted");
        long nextBalance = balance.Balance - command.ExpectedTotalPrice;
        if (nextBalance > definition.MaxBalance) throw new TradeRuleException("auction_wallet_limit");
        var escrow = await PostgresItemRepository.ReadItemForUpdateAsync(c, tx, listing.EscrowAtRegistration.ItemId, ct);
        if (escrow is null || JsonSerializer.Serialize(escrow) != JsonSerializer.Serialize(listing.EscrowAtRegistration))
            throw new TradeRuleException("auction_escrow_changed");
        policy.ValidateAuctionEscrow(escrow, listing, await Now(c, tx, ct));
        // A live catalog may lower the stack cap after registration. Preserve the original proof,
        // but do not use its old cap when merging into the buyer's inventory.
        var delivery = listing with { MaximumStackSize = Math.Min(listing.MaximumStackSize, policy.MaximumStackSize(escrow, TradeChannel.Auction)) };
        var changes = await Deliver(c, tx, command.RequestId, command.ListingId, delivery, escrow, buyerOwner,
            await Now(c, tx, ct), ct, "auction_delivery_capacity");
        now = await Now(c, tx, ct);
        var plan = listing.State.AgreePurchase(command.Buyer, command.ExpectedRevision, command.ExpectedCurrencyCode, command.ExpectedTotalPrice, now);
        policy.ValidateAuctionEscrow(escrow, listing, now);
        var sale = new AuctionSaleSettlement(command.RequestId, command.Buyer, definition, sellerWallet, plan.Amounts, now);
        var sold = listing with { State = plan.Listing, Sale = sale };
        AuctionEscrowRules.Validate(sold);
        var written = await WriteItems(c, tx, changes, now, ct);
        await using (var debit = new NpgsqlCommand("UPDATE currency_balances SET balance=@next,revision=revision+1,updated_at=@now WHERE owner_type=@type AND owner_id=@owner AND currency_code=@code AND revision=@revision AND balance=@previous", c, tx))
        {
            debit.Parameters.AddWithValue("next", nextBalance); debit.Parameters.AddWithValue("now", now);
            debit.Parameters.AddWithValue("type", buyerWallet.Scope.ToString()); debit.Parameters.AddWithValue("owner", buyerWallet.OwnerId);
            debit.Parameters.AddWithValue("code", definition.CurrencyCode); debit.Parameters.AddWithValue("revision", balance.Revision);
            debit.Parameters.AddWithValue("previous", balance.Balance);
            if (await debit.ExecuteNonQueryAsync(ct) != 1) throw new TradeRuleException("auction_wallet_revision_conflict");
        }
        var result = new AuctionPurchaseResult(command.RequestId, sold,
            new(buyerWallet, balance.Balance, nextBalance, balance.Revision), Array.AsReadOnly(written), now);
        AuctionPurchaseRules.Validate(result, command);
        await SavePurchase(c, tx, command, fingerprint, result, ct);
        await tx.CommitAsync(ct);
        return result;
    }

    private static async Task<ItemRecord[]> WriteItems(NpgsqlConnection c, NpgsqlTransaction tx,
        List<ItemRecordMutation> changes, DateTimeOffset now, CancellationToken ct)
    {
        if (changes.Count > DungeonRewardDeliveryPlanner.InventorySlotCapacity + 1
            || changes.Select(m => m.NewRecord.ItemId).Distinct().Count() != changes.Count
            || changes.Any(m => m.ExpectedRevision < 0 || m.ExpectedRevision == long.MaxValue
                || m.NewRecord.Revision != m.ExpectedRevision || !ItemRecordRules.TryValidateRecord(m.NewRecord, out _)))
            throw new TradeRuleException("invalid_auction_mutation");
        foreach (var change in changes.Where(m => m.ExpectedRevision > 0))
        {
            var current = await PostgresItemRepository.ReadItemForUpdateAsync(c, tx, change.NewRecord.ItemId, ct);
            if (current?.Revision != change.ExpectedRevision) throw new TradeRuleException("auction_item_revision_conflict");
        }
        foreach (var change in changes.Where(m => m.ExpectedRevision > 0))
            await PostgresItemRepository.StageExistingRecordAsync(c, tx, change.NewRecord.ItemId, ct);
        var written = changes.Select(m => ItemRecordRules.SnapshotWithRevision(m.NewRecord, m.ExpectedRevision + 1)).ToArray();
        foreach (var item in written)
            await PostgresItemRepository.WriteRecordAsync(c, tx, item,
                changes.Single(m => m.NewRecord.ItemId == item.ItemId).ExpectedRevision == 0, now, ct);
        return written;
    }

    private static async Task<AuctionPurchaseResult?> PurchaseReceipt(NpgsqlConnection c, NpgsqlTransaction tx,
        AuctionPurchaseCommand cmd, string fingerprint, CancellationToken ct)
    {
        await using (var q = new NpgsqlCommand("SELECT fingerprint,result::text,listing_id,buyer_account,buyer_character,currency_code,price,fee,seller_amount,committed_at FROM auction_sales WHERE request_id=@id", c, tx))
        {
            q.Parameters.AddWithValue("id", cmd.RequestId);
            await using var r = await q.ExecuteReaderAsync(ct);
            if (await r.ReadAsync(ct))
            {
                if (r.GetString(0) != fingerprint) throw new TradeRuleException("auction_idempotency_conflict");
                var result = JsonSerializer.Deserialize<AuctionPurchaseResult>(r.GetString(1)) ?? throw new InvalidDataException("Missing auction purchase receipt.");
                AuctionPurchaseRules.Validate(result, cmd);
                var sale = result.Listing.Sale!;
                if (result.Listing.State.ListingId != r.GetGuid(2) || sale.Buyer != new TradeParticipant(r.GetString(3), r.GetGuid(4))
                    || sale.Currency.CurrencyCode != r.GetString(5) || sale.Amounts != new AuctionSaleAmounts(r.GetInt64(6), r.GetInt64(8), r.GetInt64(7))
                    || result.CommittedAt != r.GetFieldValue<DateTimeOffset>(9)) throw new InvalidDataException("Auction sale index mismatch.");
                return result;
            }
        }
        await using var other = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM auction_escrow_receipts WHERE request_id=@id)", c, tx);
        other.Parameters.AddWithValue("id", cmd.RequestId);
        if ((bool)(await other.ExecuteScalarAsync(ct))!) throw new TradeRuleException("auction_idempotency_conflict");
        return null;
    }

    private static async Task SavePurchase(NpgsqlConnection c, NpgsqlTransaction tx, AuctionPurchaseCommand cmd,
        string fingerprint, AuctionPurchaseResult result, CancellationToken ct)
    {
        var sale = result.Listing.Sale!; var amount = sale.Amounts; var seller = result.Listing.State.Seller;
        await using (var update = new NpgsqlCommand("UPDATE auction_escrow_listings SET revision=@revision,closed=true,payload=@payload WHERE listing_id=@id AND revision=@expected AND NOT closed", c, tx))
        {
            update.Parameters.AddWithValue("id", cmd.ListingId); update.Parameters.AddWithValue("expected", cmd.ExpectedRevision);
            update.Parameters.AddWithValue("revision", result.Listing.State.Revision);
            update.Parameters.AddWithValue("payload", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(result.Listing));
            if (await update.ExecuteNonQueryAsync(ct) != 1) throw new TradeRuleException("auction_revision_conflict");
        }
        await using (var insert = new NpgsqlCommand("INSERT INTO auction_sales(listing_id,request_id,buyer_account,buyer_character,currency_code,price,fee,seller_amount,fingerprint,result,committed_at) VALUES(@listing,@request,@account,@character,@code,@price,@fee,@net,@fingerprint,@result,@now)", c, tx))
        {
            insert.Parameters.AddWithValue("listing", cmd.ListingId); insert.Parameters.AddWithValue("request", cmd.RequestId);
            insert.Parameters.AddWithValue("account", cmd.Buyer.AccountId); insert.Parameters.AddWithValue("character", cmd.Buyer.CharacterId);
            insert.Parameters.AddWithValue("code", sale.Currency.CurrencyCode); insert.Parameters.AddWithValue("price", amount.BuyerPayment);
            insert.Parameters.AddWithValue("fee", amount.Fee); insert.Parameters.AddWithValue("net", amount.SellerPayable);
            insert.Parameters.AddWithValue("fingerprint", fingerprint); insert.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(result));
            insert.Parameters.AddWithValue("now", result.CommittedAt); await insert.ExecuteNonQueryAsync(ct);
        }
        // No seller balance row is created or credited; proceeds survive an offline/full-wallet seller.
        await using var payable = new NpgsqlCommand("INSERT INTO auction_seller_payables(listing_id,seller_account,seller_character,currency_code,wallet_owner_type,wallet_owner_id,initial_amount,remaining_amount,revision,created_at) VALUES(@id,@account,@character,@code,@type,@owner,@amount,@amount,1,@now)", c, tx);
        payable.Parameters.AddWithValue("id", cmd.ListingId); payable.Parameters.AddWithValue("account", seller.AccountId);
        payable.Parameters.AddWithValue("character", seller.CharacterId); payable.Parameters.AddWithValue("code", sale.Currency.CurrencyCode);
        payable.Parameters.AddWithValue("type", sale.SellerWallet.Scope.ToString()); payable.Parameters.AddWithValue("owner", sale.SellerWallet.OwnerId);
        payable.Parameters.AddWithValue("amount", amount.SellerPayable); payable.Parameters.AddWithValue("now", result.CommittedAt);
        await payable.ExecuteNonQueryAsync(ct);
    }
}
