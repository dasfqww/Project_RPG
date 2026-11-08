using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryTradeSessionRepository
{
    public Task<IReadOnlyList<TradeTarget>> FindTargetsAsync(TradeParticipant actor, string name, CancellationToken ct)
    {
        name = TradeTargetSearch.Name(name);
        return Task.FromResult(game.WithTradeLocks(gate, () =>
        {
            ct.ThrowIfCancellationRequested();
            var now = gate.UtcNow;
            bool Busy(Guid id) => gate.TradeSessions.Keys.Select(trade => Load(trade)!).Any(s => Open(s)
                && s.State.ExpiresAt > now && (s.State.Initiator.CharacterId == id || s.State.Invitee.CharacterId == id));
            return game.FindTradeTargets(actor, name, now, Busy);
        }));
    }
}
