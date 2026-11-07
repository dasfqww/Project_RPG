using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using static ProjectRpg.Backend.Domain.Trading.TradeSessionRules;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryTradeSessionRepository
{
    public Task<TradePreview?> PreviewAsync(Guid tradeId, TradeParticipant actor, CancellationToken ct) =>
        Task.FromResult(game.WithTradeLocks<TradePreview?>(gate, () =>
        {
            ct.ThrowIfCancellationRequested();
            var session = Load(tradeId);
            if (session is null || !Participant(session.State, actor)) return null;
            var now = gate.UtcNow;
            Dictionary<Guid, ItemRecord> records = [];
            if (TradePreview.CanInspect(session, now))
                foreach (var id in session.State.InitiatorOffer.Items.Concat(session.State.InviteeOffer.Items).Select(i => i.ItemId).Distinct())
                    if (items.FindAsync(id, ct).GetAwaiter().GetResult() is { } item) records.Add(id, item);
            return TradePreview.Create(session, records, policy, now);
        }));
}
