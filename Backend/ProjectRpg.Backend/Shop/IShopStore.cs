using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Shop;

public sealed record ShopInventorySnapshot(ShopActor Actor, CurrencyBalance Balance,
    long AvailableBalance, IReadOnlyList<ItemRecord> Items, IReadOnlySet<Guid> ReservedItems, DateTimeOffset ObservedAt);

public sealed record ShopAssets(Guid RequestId, Guid CharacterId, DateTimeOffset ObservedAt,
    CurrencyWallet Wallet, IReadOnlyList<ItemRecord> Items);

public interface IShopStore
{
    Task<ShopInventorySnapshot> ReadAsync(ShopActor actor, ShopCatalog catalog, CancellationToken ct);
    Task<ShopReceipt> ExecuteAsync(ShopCommand command, ShopCatalog catalog, CancellationToken ct);
    Task<ShopReceipt?> FindAsync(ShopActor actor, Guid requestId, CancellationToken ct);
    Task<ShopAssets?> AssetsAsync(ShopActor actor, Guid requestId, CancellationToken ct);
}
