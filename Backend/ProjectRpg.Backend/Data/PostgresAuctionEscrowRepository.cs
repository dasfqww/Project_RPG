using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Data.PostgresTradeReservationRepository;

namespace ProjectRpg.Backend.Data;

/// <summary>Atomic registration, unsold return and purchase. Public API/DI wiring remains separate.</summary>
public sealed partial class PostgresAuctionEscrowRepository(NpgsqlDataSource dataSource, TradeItemPolicy policy,
    AuctionEscrowLimits limits) : IAuctionEscrowRepository, IAuctionPurchaseRepository
{
    public async Task<AuctionEscrowListing?> FindAsync(Guid listingId, TradeParticipant seller, CancellationToken ct)
    {
        await using var c = await dataSource.OpenConnectionAsync(ct);
        var value = await Load(c, null, listingId, ct);
        return value?.State.Seller == seller ? value : null;
    }

    public async Task<AuctionEscrowResult> ExecuteAsync(AuctionEscrowCommand command, CancellationToken ct)
    {
        command = AuctionEscrowRules.Validate(command);
        string fingerprint = AuctionEscrowRules.Fingerprint(command);
        await using var c = await dataSource.OpenConnectionAsync(ct);
        await using var tx = await c.BeginTransactionAsync(ct);
        // Same service -> account -> game rows -> item-owner ordering as admission and direct trading.
        await PostgresServiceAccess.LockAsync(c, tx, false, ct);
        await PostgresAccountAccess.LockAsync(c, tx, command.Seller.AccountId, ct);
        await Gate(c, tx, "request", command.RequestId, ct);
        var receipt = await Receipt(c, tx, command, fingerprint, ct);
        if (receipt is not null) return receipt;
        await Gate(c, tx, "listing", command.ListingId, ct);
        var before = await Load(c, tx, command.ListingId, ct);
        if (before is not null && before.State.Seller != command.Seller) throw new TradeRuleException("auction_seller_required");
        if (command.Kind == AuctionEscrowKind.Register && before is not null) throw new TradeRuleException("auction_already_exists");
        if (command.Kind != AuctionEscrowKind.Register && before is null) throw new TradeRuleException("auction_not_found");
        await Context(c, tx, command.Seller, ct);
        var owner = new ItemOwnerRef(ItemOwnerType.Character, command.Seller.CharacterId.ToString("D"));
        await PostgresItemRepository.AcquireOwnerLocksAsync(c, tx, [owner, AuctionEscrowRules.Owner(command.ListingId)], ct);
        DateTimeOffset now = await Now(c, tx, ct);
        AuctionEscrowListing listing;
        List<ItemRecordMutation> changes;
        if (command.Kind == AuctionEscrowKind.Register)
        {
            await PostgresServiceAccess.EnsureOpenAsync(c, tx, ct);
            await PostgresAccountAccess.EnsureAsync(c, tx, command.Seller.AccountId, now, ct);
            await using (var lease = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM character_session_leases WHERE character_id=@id AND expires_at>clock_timestamp())", c, tx))
            {
                lease.Parameters.AddWithValue("id", command.Seller.CharacterId);
                if ((bool)(await lease.ExecuteScalarAsync(ct))!) throw new TradeRuleException("auction_requires_lobby");
            }
            await using (var count = new NpgsqlCommand("SELECT count(*) FROM auction_escrow_listings WHERE seller_account=@account AND NOT closed", c, tx))
            {
                count.Parameters.AddWithValue("account", command.Seller.AccountId);
                if ((long)(await count.ExecuteScalarAsync(ct))! >= limits.MaximumOpenListingsPerAccount)
                    throw new TradeRuleException("auction_listing_limit");
            }
            var currency = await PostgresEconomyRepository.ReadDefinitionAsync(c, tx, command.Terms!.CurrencyCode, ct);
            if (currency is null || !currency.Enabled) throw new TradeRuleException("auction_currency_unavailable");
            ItemRecord source;
            await using (var q = new NpgsqlCommand($"SELECT {PostgresItemRepository.ItemColumns} FROM item_records WHERE item_id=@id AND owner_type='Character' AND owner_id=@owner FOR UPDATE", c, tx))
            {
                q.Parameters.AddWithValue("id", command.Item!.ItemId); q.Parameters.AddWithValue("owner", owner.OwnerId);
                await using var reader = await q.ExecuteReaderAsync(ct);
                if (!await reader.ReadAsync(ct)) throw new TradeRuleException("item_not_in_owned_inventory");
                source = PostgresItemRepository.ReadItemRecord(reader);
            }
            if ((await PostgresTradeReservationGuards.ReservedItems(c, tx, [source.ItemId], ct)).Count != 0)
                throw new TradeRuleException("auction_item_reserved");
            now = await Now(c, tx, ct);
            var state = AuctionListing.Prepare(command.ListingId, command.Seller, source, command.Item!, command.TotalPrice,
                now, command.ExpiresAt, command.Terms, policy);
            bool split = command.Item!.Quantity < source.State.Quantity;
            Guid escrowId = split ? AuctionEscrowRules.SplitItemId(command.ListingId) : source.ItemId;
            if (split)
            {
                await using var exists = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM item_records WHERE item_id=@id)", c, tx);
                exists.Parameters.AddWithValue("id", escrowId);
                if ((bool)(await exists.ExecuteScalarAsync(ct))!) throw new TradeRuleException("auction_identity_conflict");
            }
            var escrow = source with
            {
                Owner = AuctionEscrowRules.Owner(command.ListingId),
                Location = new(ItemContainerType.Auction, command.ListingId.ToString("D"), 0),
                State = source.State with { InstanceId = escrowId, Quantity = command.Item.Quantity },
                Revision = split ? 0 : source.Revision
            };
            changes = [new(escrow.Revision, escrow)];
            if (split) changes.Add(new(source.Revision, source with { State = source.State with { Quantity = source.State.Quantity - command.Item.Quantity } }));
            listing = new(state, ItemRecordRules.SnapshotWithRevision(escrow, escrow.Revision + 1),
                split ? new(source.ItemId, escrowId, command.Item.Quantity) : null, policy.MaximumStackSize(source, TradeChannel.Auction));
        }
        else
        {
            var previous = before!;
            var state = command.Kind == AuctionEscrowKind.Cancel
                ? previous.State.Cancel(command.Seller, command.ExpectedRevision, now)
                : previous.State.Expire(command.ExpectedRevision, now);
            var escrow = await PostgresItemRepository.ReadItemForUpdateAsync(c, tx, previous.EscrowAtRegistration.ItemId, ct);
            if (escrow is null || JsonSerializer.Serialize(escrow) != JsonSerializer.Serialize(previous.EscrowAtRegistration))
                throw new TradeRuleException("auction_escrow_changed");
            changes = await Deliver(c, tx, command.RequestId, command.ListingId, previous, escrow, owner, now, ct);
            listing = previous with { State = state };
        }
        AuctionEscrowRules.Validate(listing);
        var written = await WriteItems(c, tx, changes, now, ct);
        var result = new AuctionEscrowResult(command.RequestId, listing, Array.AsReadOnly(written), now);
        await Save(c, tx, command, fingerprint, result, before is null, ct);
        await tx.CommitAsync(ct);
        return result;
    }

    private async Task<List<ItemRecordMutation>> Deliver(NpgsqlConnection c, NpgsqlTransaction tx, Guid requestId, Guid listingId,
        AuctionEscrowListing listing, ItemRecord escrow, ItemOwnerRef owner, DateTimeOffset now, CancellationToken ct,
        string capacityError = "auction_return_capacity")
    {
        ItemRecord Terminal(ItemLifecycleState status) => escrow with
        { State = escrow.State with { Quantity = 0 }, Location = new(ItemContainerType.Terminal, "", -1), LifecycleState = status };
        if (escrow.Metadata.ExpiresAtUtc is { } expiry && expiry <= now) return [new(escrow.Revision, Terminal(ItemLifecycleState.Expired))];
        List<ItemRecord> inventory = [];
        await using (var q = new NpgsqlCommand($"SELECT {PostgresItemRepository.ItemColumns} FROM item_records WHERE owner_type='Character' AND owner_id=@owner AND container_type='Inventory' AND lifecycle_state='Active' ORDER BY item_id FOR UPDATE", c, tx))
        {
            q.Parameters.AddWithValue("owner", owner.OwnerId);
            await using var r = await q.ExecuteReaderAsync(ct);
            while (await r.ReadAsync(ct)) inventory.Add(PostgresItemRepository.ReadItemRecord(r));
        }
        if (inventory.Count > InventoryBagLayout.TotalSlots || inventory.Any(i => !ItemRecordRules.TryValidateRecord(i, out _)
            || !InventoryBagLayout.CanStore(i)) || inventory.Select(i => i.Location.SlotIndex).Distinct().Count() != inventory.Count)
            throw new TradeRuleException("invalid_auction_inventory");
        var held = await PostgresTradeReservationGuards.ReservedItems(c, tx, inventory.Select(i => i.ItemId), ct);
        var virtualArrival = escrow with
        { Owner = owner, Location = new(ItemContainerType.Mail, listingId.ToString("D"), 0), Revision = 0 };
        var grant = new ItemRepositoryCommitRequest(requestId, "AuctionDelivery", "auction-delivery", owner, escrow.State.Quantity, [new(0, virtualArrival)]);
        var plan = DungeonRewardDeliveryPlanner.PlanAuctionReturn(grant, [listing.MaximumStackSize], inventory, held);
        var changes = plan.Mutations.Select(m => m.NewRecord.ItemId == escrow.ItemId
            ? new ItemRecordMutation(escrow.Revision, m.NewRecord with { Revision = escrow.Revision }) : m).ToList();
        bool returnsRecord = changes.Any(m => m.NewRecord.ItemId == escrow.ItemId);
        if (!returnsRecord) changes.Add(new(escrow.Revision, Terminal(ItemLifecycleState.Consumed)));
        if (returnsRecord)
        {
            await using var count = new NpgsqlCommand("SELECT count(*) FROM item_records WHERE owner_type='Character' AND owner_id=@owner AND lifecycle_state='Active'", c, tx);
            count.Parameters.AddWithValue("owner", owner.OwnerId);
            if ((long)(await count.ExecuteScalarAsync(ct))! >= limits.MaximumOwnedRecords)
                throw new TradeRuleException(capacityError);
        }
        return changes;
    }

    private static async Task Gate(NpgsqlConnection c, NpgsqlTransaction tx, string kind, Guid id, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))", c, tx);
        q.Parameters.AddWithValue("key", "auction_" + kind + ":" + id.ToString("D")); await q.ExecuteNonQueryAsync(ct);
    }

    private static async Task<AuctionEscrowListing?> Load(NpgsqlConnection c, NpgsqlTransaction? tx, Guid id, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT payload::text,seller_account,seller_character,escrow_item,expires_at,revision,closed FROM auction_escrow_listings WHERE listing_id=@id", c, tx);
        q.Parameters.AddWithValue("id", id);
        await using var r = await q.ExecuteReaderAsync(ct);
        if (!await r.ReadAsync(ct)) return null;
        var result = JsonSerializer.Deserialize<AuctionEscrowListing>(r.GetString(0)) ?? throw new InvalidDataException("Missing auction payload.");
        AuctionEscrowRules.Validate(result);
        if (result.State.ListingId != id || result.State.Seller != new TradeParticipant(r.GetString(1), r.GetGuid(2))
            || result.EscrowAtRegistration.ItemId != r.GetGuid(3) || SqlInstant(result.State.ExpiresAt) != r.GetFieldValue<DateTimeOffset>(4)
            || result.State.Revision != r.GetInt64(5) || (result.State.Status != AuctionListingStatus.Active) != r.GetBoolean(6))
            throw new InvalidDataException("Auction index does not match its payload.");
        return result;
    }

    private static async Task<AuctionEscrowResult?> Receipt(NpgsqlConnection c, NpgsqlTransaction tx,
        AuctionEscrowCommand cmd, string fingerprint, CancellationToken ct)
    {
        await using var q = new NpgsqlCommand("SELECT fingerprint,result::text FROM auction_escrow_receipts WHERE request_id=@id", c, tx);
        q.Parameters.AddWithValue("id", cmd.RequestId);
        AuctionEscrowResult? result;
        await using (var r = await q.ExecuteReaderAsync(ct))
        {
            if (!await r.ReadAsync(ct)) result = null;
            else
            {
                if (r.GetString(0) != fingerprint) throw new TradeRuleException("auction_idempotency_conflict");
                result = JsonSerializer.Deserialize<AuctionEscrowResult>(r.GetString(1)) ?? throw new InvalidDataException("Missing auction receipt.");
            }
        }
        if (result is null)
        {
            await using var other = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM auction_sales WHERE request_id=@id UNION ALL SELECT 1 FROM auction_claim_receipts WHERE request_id=@id)", c, tx);
            other.Parameters.AddWithValue("id", cmd.RequestId);
            if ((bool)(await other.ExecuteScalarAsync(ct))!) throw new TradeRuleException("auction_idempotency_conflict");
            return null;
        }
        AuctionEscrowRules.Validate(result.Listing);
        if (result.RequestId != cmd.RequestId || result.Listing.State.ListingId != cmd.ListingId || result.Listing.State.Seller != cmd.Seller)
            throw new InvalidDataException("Auction receipt scope mismatch.");
        return result;
    }

    private static async Task Save(NpgsqlConnection c, NpgsqlTransaction tx, AuctionEscrowCommand cmd,
        string fingerprint, AuctionEscrowResult result, bool insert, CancellationToken ct)
    {
        var listing = result.Listing;
        await using (var q = new NpgsqlCommand(insert
            ? "INSERT INTO auction_escrow_listings(listing_id,seller_account,seller_character,escrow_item,expires_at,revision,closed,payload) VALUES(@id,@account,@character,@item,@expiry,@revision,@closed,@payload)"
            : "UPDATE auction_escrow_listings SET revision=@revision,closed=@closed,payload=@payload WHERE listing_id=@id AND revision=@expected", c, tx))
        {
            q.Parameters.AddWithValue("id", cmd.ListingId); q.Parameters.AddWithValue("account", cmd.Seller.AccountId);
            q.Parameters.AddWithValue("character", cmd.Seller.CharacterId); q.Parameters.AddWithValue("item", listing.EscrowAtRegistration.ItemId);
            q.Parameters.AddWithValue("expiry", SqlInstant(listing.State.ExpiresAt)); q.Parameters.AddWithValue("revision", listing.State.Revision);
            q.Parameters.AddWithValue("closed", listing.State.Status != AuctionListingStatus.Active);
            q.Parameters.AddWithValue("payload", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(listing));
            q.Parameters.AddWithValue("expected", cmd.ExpectedRevision);
            if (await q.ExecuteNonQueryAsync(ct) != 1) throw new TradeRuleException("auction_revision_conflict");
        }
        await using var receipt = new NpgsqlCommand("INSERT INTO auction_escrow_receipts(request_id,listing_id,actor_account,actor_character,fingerprint,result,committed_at) VALUES(@id,@listing,@account,@character,@fingerprint,@result,@now)", c, tx);
        receipt.Parameters.AddWithValue("id", cmd.RequestId); receipt.Parameters.AddWithValue("listing", cmd.ListingId);
        receipt.Parameters.AddWithValue("account", cmd.Seller.AccountId); receipt.Parameters.AddWithValue("character", cmd.Seller.CharacterId);
        receipt.Parameters.AddWithValue("fingerprint", fingerprint); receipt.Parameters.AddWithValue("result", NpgsqlDbType.Jsonb, JsonSerializer.Serialize(result));
        receipt.Parameters.AddWithValue("now", result.CommittedAt); await receipt.ExecuteNonQueryAsync(ct);
    }

    // Keep the exact instant in the immutable command/JSON receipt. SQL's index encodes microseconds.
    private static DateTimeOffset SqlInstant(DateTimeOffset value) => new(value.UtcTicks / 10 * 10, TimeSpan.Zero);
}
