using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    internal void ValidateTradeAssets(InMemoryTransactionGate gate, InMemoryItemRepository items)
    {
        if (!ReferenceEquals(_starterTransactionGate, gate) || !ReferenceEquals(_starterItems, items))
            throw new InvalidOperationException("Trading requires the game's shared item repository and transaction gate.");
    }

    // Existing equipment, pickup and operations use dungeon -> item -> character, never the reverse.
    // Concrete Memory repositories finish synchronously; the callback must not await or invoke external I/O.
    internal T WithTradeLocks<T>(InMemoryTransactionGate gate, Func<T> action)
    {
        lock (_dungeonGate)
        lock (gate.SyncRoot)
            return action();
    }

    internal void EnsureTradeAccess(IReadOnlyList<TradeParticipant> people, DateTimeOffset now)
    {
        if (!Monitor.IsEntered(_dungeonGate)) throw new InvalidOperationException("Game state gate required.");
        EnsureServiceOpen();
        foreach (var p in people) EnsureAccountAllowed(p.AccountId, now);
        if (people.Any(p => _characterLeases.TryGetValue(p.CharacterId, out var lease) && lease.ExpiresAt > now))
            throw new TradeRuleException("trade_requires_lobby");
    }
}
