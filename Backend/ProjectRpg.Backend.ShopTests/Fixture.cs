using Microsoft.Extensions.Logging.Abstractions;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Shop;

namespace ProjectRpg.Backend.ShopTests;

public sealed class Fixture
{
    private static int sequence;
    private readonly Action<bool, string> assert;
    public IGameRepository Game { get; }
    public IItemRepository Items { get; }
    public IEconomyRepository Economy { get; }
    public ITradeReservationRepository Reservations { get; }
    public IShopStore Store { get; }
    public NpgsqlDataSource? Data { get; }
    public static ShopOffer Potion => new("potion", "회복 물약", "GameItem", "GameItem.Consume.Potion.Red.Large", 1, 10, 100, 100, ItemBindState.Unbound);
    public static ShopSaleRule Junk => new("scrap", "판매용 파편", "GameItem", "GameItem.Junk.Scrap", 1, 2, 100, 100, [ItemBindState.Unbound]);
    public ShopCatalog Catalog { get; } = new(true, "general", "보급 상인", "ShopGold", [Potion], [Junk]);
    public DateTimeOffset Now => DateTimeOffset.UtcNow;

    private Fixture(Action<bool, string> assert, NpgsqlDataSource? data)
    {
        this.assert = assert; Data = data;
        var policy = new TradeItemPolicy([
            new(Potion.DefinitionType, Potion.DefinitionName, Potion.DefinitionVersion, true, true, Potion.MaximumStackSize),
            new(Junk.DefinitionType, Junk.DefinitionName, Junk.DefinitionVersion, true, true, Junk.MaximumStackSize)]);
        if (data is null)
        {
            var gate = new InMemoryTransactionGate();
            var items = new InMemoryItemRepository(gate); var economy = new InMemoryEconomyRepository(gate);
            var game = new InMemoryGameRepository(gate, items);
            Game = game; Items = items; Economy = economy;
            Reservations = new InMemoryTradeReservationRepository(gate, items, economy, game, policy);
            Store = new InMemoryShopStore(gate, game, items, economy);
        }
        else
        {
            var items = new PostgresItemRepository(data, NullLogger<PostgresItemRepository>.Instance);
            Game = new PostgresGameRepository(data, NullLogger<PostgresGameRepository>.Instance, equipmentItems: items);
            Items = items; Economy = new PostgresEconomyRepository(data);
            Reservations = new PostgresTradeReservationRepository(data, policy);
            Store = new PostgresShopStore(data);
        }
    }

    public static async Task<Fixture> Create(Action<bool, string> assert, NpgsqlDataSource? data = null)
    {
        var f = new Fixture(assert, data);
        await f.Game.EnsureCreatedAsync(default);
        await f.Economy.UpsertDefinitionAsync(new("ShopGold", "상점 시험 재화", CurrencyScope.Character, 10_000, true), f.Now, default);
        return f;
    }
    public async Task<ShopActor> Person(long amount = 1000, string? account = null)
    {
        int n = Interlocked.Increment(ref sequence);
        account ??= (76561900000000000L + n).ToString(System.Globalization.CultureInfo.InvariantCulture);
        await Game.UpsertAccountAsync(new(account, account, false, false), Now, default);
        var character = await Game.CreateCharacterAsync(account, "Shop" + n, Now, default);
        var actor = new ShopActor(account, character.CharacterId);
        if (amount > 0) await Fund(actor, amount);
        return actor;
    }
    public async Task Fund(ShopActor actor, long amount, string code = "ShopGold")
    {
        var r = await Change(actor, amount, code);
        assert(r.Status == CurrencyTransactionStatus.Committed, "shop fixture funding: " + r.Status);
    }
    public async Task<CurrencyTransactionResult> Change(ShopActor actor, long delta, string code = "ShopGold") =>
        await Economy.CommitAsync(new(Guid.NewGuid(), actor.CharacterId, Guid.NewGuid(), "Test.ShopFund", Guid.NewGuid().ToString("N"),
            "shop fixture", [new(code, delta)]), (await Game.GetCharacterEconomyContextAsync(actor.CharacterId, default))!, Now, default);
    public async Task<long> Balance(ShopActor actor, string code = "ShopGold") =>
        (await Economy.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(actor.CharacterId, default))!, default))
        .Balances.Single(b => b.Definition.CurrencyCode == code).Balance;
    public async Task<IReadOnlyList<ItemRecord>> Owned(ShopActor actor) =>
        await Items.FindByOwnerAsync(new(ItemOwnerType.Character, actor.CharacterId.ToString("D")), false, 501, default);
    public async Task<ItemRecord> Seed(ShopActor actor, string name, int slot, int quantity = 4,
        bool locked = false, DateTimeOffset? expiry = null, IReadOnlyList<string>? tags = null)
    {
        var owner = new ItemOwnerRef(ItemOwnerType.Character, actor.CharacterId.ToString("D"));
        var item = new ItemRecord("GameItem", name, 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, slot),
            new(Guid.NewGuid(), 0, quantity, tags ?? [], []), 0, ItemLifecycleState.Active,
            new(ItemBindState.Unbound, new(0, 0), expiry, "Test.ShopSeed", locked));
        var result = await Items.CommitAsync(new(Guid.NewGuid(), "Test.ShopSeed", Guid.NewGuid().ToString("N"), owner,
            quantity, [new(0, item)]), Now, default);
        assert(result.Status == ItemRepositoryCommitStatus.Committed, "shop fixture seed: " + result.Status);
        return result.Records.Single();
    }
    public ShopCommand Buy(ShopActor actor, int quantity = 2) => new(Guid.NewGuid(), actor, Catalog.ShopId,
        Catalog.PolicyTag, "buy", "potion", quantity, Potion.UnitPrice);
    public ShopCommand Sell(ShopActor actor, ItemRecord item, int quantity = 2) => new(Guid.NewGuid(), actor, Catalog.ShopId,
        Catalog.PolicyTag, "sell", "scrap", quantity, Junk.UnitPrice, item.ItemId, item.Revision);
    public TradeReservationCommand Reservation(ShopActor actor, long money = 0, ItemRecord? item = null) =>
        new(Guid.NewGuid(), Guid.NewGuid(), 1, new(actor.AccountId, actor.CharacterId), Catalog.CurrencyCode,
            new TradeOffer(item is null ? [] : [new TradeOfferedItem(item.ItemId, item.Revision, item.State.Quantity)], money), Now.AddMinutes(5));
}
