using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    private readonly Dictionary<(Guid SessionId, string ServerId, Guid RequestId), JoinTicketConsumptionCancellation>
        _joinTicketConsumptionCancellations = new();

    public Task<JoinTicketConsumptionResolution?> SealJoinTicketConsumptionAsync(
        string tokenHash, string serverId, Guid dungeonSessionId, Guid consumptionRequestId,
        string expectedSteamId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            now = connectionClock?.GetUtcNow() ?? now;
            if (consumptionRequestId == Guid.Empty
                || !_dungeonSessions.TryGetValue(dungeonSessionId, out var session)
                || session.ServerId != serverId || session.ExpiresAt <= now
                || session.State is not (DungeonSessionState.Loading or DungeonSessionState.InProgress))
                return Task.FromResult<JoinTicketConsumptionResolution?>(null);

            var key = (dungeonSessionId, serverId, consumptionRequestId);
            if (_joinTicketConsumptions.TryGetValue(key, out var receipt))
            {
                if (receipt.TokenHash != tokenHash || receipt.Admission.SteamId != expectedSteamId)
                    return Task.FromResult<JoinTicketConsumptionResolution?>(null);
                return Task.FromResult<JoinTicketConsumptionResolution?>(new("Recorded",
                    new(dungeonSessionId, serverId, consumptionRequestId, receipt.Admission.CharacterId,
                        receipt.Admission.SteamId, receipt.Admission.ConnectionGeneration, receipt.ConsumedAt), null));
            }
            if (_joinTicketConsumptionCancellations.TryGetValue(key, out var cancelled))
                return Task.FromResult<JoinTicketConsumptionResolution?>(cancelled.TicketHash == tokenHash
                    && cancelled.ExpectedSteamId == expectedSteamId ? new("Cancelled", null, cancelled) : null);

            // An expired ticket may still prove non-consumption. Do not clean it
            // first, or infer anything from a ticket already removed by cleanup.
            if (_joinTicketConsumptions.Values.Any(value => value.TokenHash == tokenHash)
                || _joinTicketConsumptionCancellations.Values.Any(value => value.TicketHash == tokenHash)
                || !_joinTickets.TryGetValue(tokenHash, out var ticket)
                || ticket.DungeonSessionId != dungeonSessionId || ticket.SteamId != expectedSteamId)
                return Task.FromResult<JoinTicketConsumptionResolution?>(null);

            cancelled = new(dungeonSessionId, serverId, consumptionRequestId, tokenHash, expectedSteamId, now);
            _joinTicketConsumptionCancellations.Add(key, cancelled);
            _joinTickets.TryRemove(tokenHash, out _);
            return Task.FromResult<JoinTicketConsumptionResolution?>(new("Cancelled", null, cancelled));
        }
    }
}
