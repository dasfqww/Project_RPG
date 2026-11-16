using ProjectRpg.Backend.Shop;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    internal void EnsureShopAccess(ShopActor actor, DateTimeOffset now)
    {
        if (!Monitor.IsEntered(_dungeonGate)) throw new InvalidOperationException("Game state gate required.");
        EnsureServiceOpen();
        EnsureAccountAllowed(actor.AccountId, now);
        if (_characterLeases.TryGetValue(actor.CharacterId, out var lease) && lease.ExpiresAt > now)
            throw new ShopRuleException("shop_requires_lobby");
    }
}
