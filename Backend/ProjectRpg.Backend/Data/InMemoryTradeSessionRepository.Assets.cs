using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Domain.Trading.TradeSessionRules;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryTradeSessionRepository
{
    public Task<TradeAssets?> AssetsAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct) =>
        Task.FromResult(game.WithTradeLocks<TradeAssets?>(gate, () =>
        {
            ct.ThrowIfCancellationRequested();
            var session = Load(tradeId);
            if (session is null || !Participant(session.State, actor)) return null;
            if (!session.Completed) throw new TradeRuleException("trade_not_completed");
            var context = Context(actor, ct);
            var records = items.FindByOwnerAsync(new(ItemOwnerType.Character, actor.CharacterId.ToString("D")),
                false, TradeAssets.MaximumItems + 1, ct).GetAwaiter().GetResult();
            var wallet = economy.LoadWalletAsync(context, ct).GetAwaiter().GetResult();
            return TradeAssets.Create(session, actor, wallet, records, gate.UtcNow);
        }));
}
