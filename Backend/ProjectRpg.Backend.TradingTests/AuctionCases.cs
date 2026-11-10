using System.Numerics;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static class AuctionCases
{
    private static readonly DateTimeOffset Now = new(2026, 11, 10, 0, 0, 0, TimeSpan.Zero);
    private static readonly TradeParticipant Seller = new("seller", Guid.NewGuid()), Buyer = new("buyer", Guid.NewGuid());
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, false, true, 100)]);
    private static readonly AuctionTerms Terms = new("Gold", 1, long.MaxValue, TimeSpan.FromMinutes(1), TimeSpan.FromDays(7), 500);
    private static int checks;

    public static int Run()
    {
        try
        {
            TermsAndRegistration(); ItemAdmission(); Purchase(); Termination(); Fees(); Claims();
            Console.WriteLine($"PASS Auction domain: 6 groups, {checks} assertions. Proposed state and integer plans only; no escrow/DB/CAS winner/API/UE claim.");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }

    private static ItemRecord Item() => new("Material", "Ore", 1,
        new(ItemOwnerType.Character, Seller.CharacterId.ToString("D")),
        new(ItemContainerType.Inventory, Seller.CharacterId.ToString("D"), 7),
        new(Guid.NewGuid(), 3, 10, ["Item.Ore"], [new("Stat.Value", 4)]), 5, ItemLifecycleState.Active,
        new(ItemBindState.Unbound, new(0, 0), null, "Test", false));

    private static AuctionListing Listing(ItemRecord? item = null, DateTimeOffset? expires = null,
        AuctionTerms? terms = null, long price = 699, int quantity = 3)
    {
        item ??= Item();
        return AuctionListing.Prepare(Guid.NewGuid(), Seller, item, new(item.ItemId, item.Revision, quantity),
            price, Now, expires ?? Now.AddHours(1), terms ?? Terms, Policy);
    }

    private static void TermsAndRegistration()
    {
        foreach (string code in new[] { "", "Gold ", "Gold\n", "골드", new string('x', 65) })
            Reject(() => new AuctionTerms(code, 1, 100, TimeSpan.FromMinutes(1), TimeSpan.FromHours(1), 0), "invalid_auction_terms");
        foreach (var bounds in new[] { (0L, 10L), (-1L, 10L), (10L, 9L) })
            Reject(() => new AuctionTerms("Gold", bounds.Item1, bounds.Item2, TimeSpan.FromMinutes(1), TimeSpan.FromHours(1), 0), "invalid_auction_terms");
        foreach (var durations in new[] { (TimeSpan.Zero, TimeSpan.FromHours(1)), (TimeSpan.FromHours(2), TimeSpan.FromHours(1)) })
            Reject(() => new AuctionTerms("Gold", 1, 100, durations.Item1, durations.Item2, 0), "invalid_auction_terms");
        foreach (int rate in new[] { -1, 10001 })
            Reject(() => new AuctionTerms("Gold", 1, 100, TimeSpan.FromMinutes(1), TimeSpan.FromHours(1), rate), "invalid_auction_terms");
        var item = Item();
        var listing = Listing(item);
        Check(listing.Status == AuctionListingStatus.Active && listing.Revision == 1 && listing.Buyer is null, "Registration is an active proposed snapshot.");
        Check(listing.Item == new TradeOfferedItem(item.ItemId, 5, 3) && listing.TotalPrice == 699, "Quantity is a listed subset; price is its entire total.");
        Check(item.Owner.OwnerId == Seller.CharacterId.ToString("D") && item.Location.ContainerType == ItemContainerType.Inventory
            && item.State.Quantity == 10 && item.Revision == 5, "Preparing listing never moves, decrements or reserves the source item.");
        Check(Listing(expires: Now.AddMinutes(1)).ExpiresAt == Now.AddMinutes(1), "Minimum duration inclusive.");
        Check(Listing(expires: Now.AddDays(7), price: long.MaxValue).TotalPrice == long.MaxValue, "Maximum duration and int64 price inclusive.");
        Reject(() => Listing(expires: Now.AddMinutes(1).AddTicks(-1)), "invalid_auction_duration");
        Reject(() => Listing(expires: Now.AddDays(7).AddTicks(1)), "invalid_auction_duration");
        Reject(() => Listing(expires: Now), "invalid_auction_duration");
        Reject(() => Listing(price: 0), "invalid_auction_price");
        Reject(() => Listing(terms: new("Gold", 1, 698, TimeSpan.FromMinutes(1), TimeSpan.FromDays(7), 0)), "invalid_auction_price");
        Reject(() => AuctionListing.Prepare(Guid.Empty, Seller, item, new(item.ItemId, 5, 1), 1, Now, Now.AddHours(1), Terms, Policy), "invalid_auction_listing");
        foreach (var seller in new[] { Seller with { AccountId = "" }, Seller with { AccountId = "x\n" }, Seller with { CharacterId = Guid.Empty } })
            Reject(() => AuctionListing.Prepare(Guid.NewGuid(), seller, item, new(item.ItemId, 5, 1), 1, Now, Now.AddHours(1), Terms, Policy), "invalid_auction_listing");
        var offset = Now.ToOffset(TimeSpan.FromHours(9));
        var normalized = AuctionListing.Prepare(Guid.NewGuid(), Seller, item, new(item.ItemId, 5, 1), 1,
            offset, offset.AddHours(1), Terms, Policy);
        Check(normalized.CreatedAt.Offset == TimeSpan.Zero && normalized.ExpiresAt == Now.AddHours(1), "Store instants use UTC.");
    }

    private static void ItemAdmission()
    {
        var item = Item();
        Reject(() => AuctionListing.Prepare(Guid.NewGuid(), Seller, item, new(item.ItemId, 4, 1), 1, Now, Now.AddHours(1), Terms, Policy), "item_revision_conflict");
        foreach (int quantity in new[] { -1, 0, 11 }) Reject(() => Listing(item, quantity: quantity), "invalid_trade_quantity");
        foreach (var bind in new[] { ItemBindState.CharacterBound, ItemBindState.AccountBound })
            Reject(() => Listing(item with { Metadata = item.Metadata with { BindState = bind } }), "item_bound");
        Check(Listing(item with { Metadata = item.Metadata with { BindState = ItemBindState.BindOnEquip } }).Status == AuctionListingStatus.Active, "Unworn BindOnEquip policy is shared with direct trade.");
        Reject(() => Listing(item with { Metadata = item.Metadata with { IsLocked = true } }), "item_unavailable");
        Reject(() => Listing(item with { Metadata = item.Metadata with { ExpiresAtUtc = Now } }), "item_unavailable");
        Reject(() => Listing(item with { Metadata = item.Metadata with { ExpiresAtUtc = Now.AddMinutes(30) } }), "auction_item_expires_before_listing");
        Check(Listing(item with { Metadata = item.Metadata with { ExpiresAtUtc = Now.AddHours(1) } }).ExpiresAt == Now.AddHours(1), "Item expiry equals listing expiry safely.");
        Reject(() => Listing(item with { Owner = new(ItemOwnerType.Character, Buyer.CharacterId.ToString("D")) }), "item_not_in_owned_inventory");
        Reject(() => Listing(item with { Owner = new(ItemOwnerType.Character, Buyer.CharacterId.ToString("D")), Location = new(ItemContainerType.Inventory, Buyer.CharacterId.ToString("D"), 7) }), "item_not_in_owned_inventory");
        Reject(() => Listing(item with { Location = item.Location with { ContainerType = ItemContainerType.Equipment } }), "item_not_in_owned_inventory");
        Reject(() => Listing(item with { DefinitionVersion = 2 }), "item_trade_not_allowed");
        foreach (var policy in new[] { new TradeItemPolicy([]), new TradeItemPolicy([new("Material", "Ore", 1, true, false, 100)]) })
            Reject(() => AuctionListing.Prepare(Guid.NewGuid(), Seller, item, new(item.ItemId, 5, 1), 1, Now, Now.AddHours(1), Terms, policy), "item_trade_not_allowed");
        Reject(() => Policy.ValidateOfferItem(item, Seller, new(item.ItemId, 5, 1), TradeChannel.Direct, Now), "item_trade_not_allowed");
        Reject(() => Listing(item with { State = item.State with { Quantity = 101 } }), "invalid_trade_quantity");
    }

    private static void Purchase()
    {
        var listing = Listing();
        foreach (var buyer in new[] { Seller, Buyer with { AccountId = Seller.AccountId }, Buyer with { CharacterId = Seller.CharacterId } })
            Reject(() => listing.AgreePurchase(buyer, 1, "Gold", 699, Now), "auction_self_purchase");
        foreach (var buyer in new[] { Buyer with { AccountId = "" }, Buyer with { AccountId = new string('x', 129) }, Buyer with { CharacterId = Guid.Empty } })
            Reject(() => listing.AgreePurchase(buyer, 1, "Gold", 699, Now), "invalid_auction_buyer");
        foreach (long revision in new[] { 0L, -1L, 2L, long.MaxValue })
            Reject(() => listing.AgreePurchase(Buyer, revision, "Gold", 699, Now), "auction_revision_conflict");
        Reject(() => listing.AgreePurchase(Buyer, 1, "gold", 699, Now), "auction_price_conflict");
        Reject(() => listing.AgreePurchase(Buyer, 1, "Gold", 698, Now), "auction_price_conflict");
        Reject(() => listing.AgreePurchase(Buyer, 1, "Gold", 699, listing.ExpiresAt), "auction_expired");
        Reject(() => listing.AgreePurchase(Buyer, 1, "Gold", 699, Now.AddTicks(-1)), "invalid_auction_time");
        var plan = listing.AgreePurchase(Buyer, 1, "Gold", 699, listing.ExpiresAt.AddTicks(-1));
        Check(plan.Listing.Status == AuctionListingStatus.SaleAgreed && plan.Listing.Buyer == Buyer && plan.Listing.Revision == 2, "Last live instant creates a sale intent bound to buyer.");
        Check(plan.Amounts == new AuctionSaleAmounts(699, 665, 34) && plan.Listing.Item.Quantity == 3, "One whole listing, total price once and separate seller payable.");
        Check(listing.Status == AuctionListingStatus.Active && listing.Buyer is null && listing.Revision == 1, "Original snapshot remains unchanged; repository still owes an atomic CAS.");
        Reject(() => plan.Listing.AgreePurchase(Buyer, 2, "Gold", 699, Now), "auction_not_active");
        Reject(() => plan.Listing.Cancel(Seller, 2, Now), "auction_not_active");
        Reject(() => plan.Listing.Expire(2, listing.ExpiresAt), "auction_not_active");
    }

    private static void Termination()
    {
        var listing = Listing();
        Reject(() => listing.Cancel(Buyer, 1, Now), "auction_seller_required");
        Reject(() => listing.Cancel(Seller with { AccountId = Buyer.AccountId }, 1, Now), "auction_seller_required");
        Reject(() => listing.Cancel(Seller, 2, Now), "auction_revision_conflict");
        Reject(() => listing.Expire(2, listing.ExpiresAt), "auction_revision_conflict");
        Reject(() => listing.Expire(1, listing.ExpiresAt.AddTicks(-1)), "auction_not_expired");
        var cancelled = listing.Cancel(Seller, 1, Now);
        var expired = listing.Expire(1, listing.ExpiresAt);
        Check(cancelled.Status == AuctionListingStatus.Cancelled && cancelled.Revision == 2 && cancelled.Buyer is null, "Seller cancellation is terminal.");
        Check(expired.Status == AuctionListingStatus.Expired && expired.Revision == 2 && expired.Buyer is null, "Server expiry is terminal.");
        Check(listing.Cancel(Seller, 1, listing.ExpiresAt).Status == AuctionListingStatus.Expired, "Cancel at the deadline proposes expiry rather than a late sale.");
        foreach (var closed in new[] { cancelled, expired })
        {
            Reject(() => closed.AgreePurchase(Buyer, 2, "Gold", 699, Now), "auction_not_active");
            Reject(() => closed.Cancel(Seller, 2, Now), "auction_not_active");
            Reject(() => closed.Expire(2, listing.ExpiresAt), "auction_not_active");
        }
    }

    private static void Fees()
    {
        foreach (long amount in new[] { 1L, 19L, 20L, 699L, 9007199254740993L, long.MaxValue })
        foreach (int rate in new[] { 0, 1, 333, 500, 9999, 10000 })
        {
            var result = AuctionMoney.Sale(amount, rate);
            var expectedFee = (BigInteger)amount * rate / 10000;
            Check(result.BuyerPayment == amount && result.Fee == expectedFee && result.SellerPayable >= 0
                && (BigInteger)result.SellerPayable + result.Fee == amount, "Exact sale conservation and floor rounding against arbitrary precision oracle.");
        }
        Check(AuctionMoney.Sale(1, 9999).Fee == 0, "Small fees round down; no floating-point or per-unit rounding.");
        foreach (long amount in new[] { 0L, -1L, long.MinValue }) Reject(() => AuctionMoney.Sale(amount, 0), "invalid_auction_price");
        foreach (int rate in new[] { -1, 10001, int.MaxValue }) Reject(() => AuctionMoney.Sale(1, rate), "invalid_auction_price");
        var random = new Random(401);
        for (int i = 0; i < 500; ++i)
        {
            long amount = random.NextInt64(1, long.MaxValue); int rate = random.Next(10001);
            var result = AuctionMoney.Sale(amount, rate);
            Check(result.Fee == (BigInteger)amount * rate / 10000 && (BigInteger)result.SellerPayable + result.Fee == amount, "Wide int64 price and server rate conserve money.");
        }
    }

    private static void Claims()
    {
        Check(AuctionMoney.Claim(665, 665, 999, 1000) == new AuctionClaimAmounts(1, 664, 1000), "Credit only headroom, keep all overflow payable.");
        Check(AuctionMoney.Claim(665, 100, 10, 1000) == new AuctionClaimAmounts(100, 565, 110), "Explicit partial claim.");
        Check(AuctionMoney.Claim(5, 99, 0, 100) == new AuctionClaimAmounts(5, 0, 5), "Claim never exceeds payable.");
        Check(AuctionMoney.Claim(665, 665, 1000, 1000) == new AuctionClaimAmounts(0, 665, 1000), "Full wallet keeps payable unchanged.");
        Check(AuctionMoney.Claim(0, 1, 0, 100) == new AuctionClaimAmounts(0, 0, 0), "Already empty payable is a zero plan.");
        Check(AuctionMoney.Claim(long.MaxValue, long.MaxValue, long.MaxValue - 1, long.MaxValue)
            == new AuctionClaimAmounts(1, long.MaxValue - 1, long.MaxValue), "Int64 cap without overflow.");
        foreach (var values in new[] { (-1L, 1L, 0L, 1L), (1L, 0L, 0L, 1L), (1L, -1L, 0L, 1L), (1L, 1L, -1L, 1L), (1L, 1L, 2L, 1L) })
            Reject(() => AuctionMoney.Claim(values.Item1, values.Item2, values.Item3, values.Item4), "invalid_auction_claim");
        var random = new Random(402);
        for (int i = 0; i < 500; ++i)
        {
            long remaining = random.NextInt64(long.MaxValue), request = random.NextInt64(1, long.MaxValue);
            long balance = random.NextInt64(long.MaxValue), cap = random.NextInt64(balance, long.MaxValue);
            var result = AuctionMoney.Claim(remaining, request, balance, cap);
            Check(result.Paid >= 0 && result.Paid <= request && result.NewWalletBalance <= cap
                && (BigInteger)result.RemainingPayable + result.Paid == remaining
                && (BigInteger)balance + result.Paid == result.NewWalletBalance, "Partial claim conserves payable and wallet within cap.");
        }
    }

    private static void Check(bool value, string message) { if (!value) throw new Exception(message); ++checks; }
    private static void Reject(Action action, string code)
    {
        try { action(); }
        catch (TradeRuleException e) { Check(e.Code == code, $"Expected {code}, got {e.Code}"); return; }
        throw new Exception("Expected rejection: " + code);
    }
}
