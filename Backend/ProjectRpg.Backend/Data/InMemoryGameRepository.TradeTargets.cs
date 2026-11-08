using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    // Caller holds dungeon -> shared asset gate, covering lease/restriction/trade state together.
    internal IReadOnlyList<TradeTarget> FindTradeTargets(TradeParticipant actor, string name,
        DateTimeOffset now, Func<Guid, bool> busy)
    {
        if (!Monitor.IsEntered(_dungeonGate)) throw new InvalidOperationException("Game state gate required.");
        if (!_characters.TryGetValue(actor.CharacterId, out var own) || own.SteamId != actor.AccountId)
            throw new TradeRuleException("character_not_owned");
        EnsureTradeAccess([actor], now);
        if (busy(actor.CharacterId)) throw new TradeRuleException("character_already_trading");
        if (!_rosters.TryGetValue(own.RosterId, out var roster)) return [];
        var candidate = _characters.Values.FirstOrDefault(c => string.Equals(c.Name, name, StringComparison.OrdinalIgnoreCase));
        if (candidate is null || candidate.SteamId == actor.AccountId
            || !_rosters.TryGetValue(candidate.RosterId, out var targetRoster) || targetRoster.WorldId != roster.WorldId
            || !AccountAllowed(candidate.SteamId, now)
            || (_characterLeases.TryGetValue(candidate.CharacterId, out var lease) && lease.ExpiresAt > now)
            || busy(candidate.CharacterId)) return [];
        return [new(candidate.CharacterId, candidate.Name)];
    }
}
