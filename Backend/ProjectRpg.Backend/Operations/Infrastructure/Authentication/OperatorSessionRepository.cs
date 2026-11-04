namespace ProjectRpg.Backend.Operations.Infrastructure.Authentication;

public sealed record OperatorSession(byte[] KeyHash, string OperatorId, byte[] ProtectedTicket,
    string AuthEpoch, DateTimeOffset CreatedAt, DateTimeOffset LastSeenAt, DateTimeOffset AbsoluteExpiresAt);

public interface IOperatorSessionRepository
{
    Task StoreAsync(OperatorSession session, CancellationToken ct);
    Task<OperatorSession?> TouchAsync(byte[] keyHash, string operatorId, string epoch, DateTimeOffset now, CancellationToken ct);
    Task RevokeAsync(byte[] keyHash, DateTimeOffset now, CancellationToken ct);
    Task PurgeAsync(DateTimeOffset now, CancellationToken ct);
}

public sealed class MemoryOperatorSessionRepository : IOperatorSessionRepository
{
    private readonly object gate = new();
    private readonly Dictionary<string, OperatorSession> sessions = [];
    public Task StoreAsync(OperatorSession session, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (gate) sessions.Add(Convert.ToHexString(session.KeyHash), session with { ProtectedTicket = session.ProtectedTicket.ToArray() });
        return Task.CompletedTask;
    }
    public Task<OperatorSession?> TouchAsync(byte[] keyHash, string operatorId, string epoch, DateTimeOffset now, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (gate)
        {
            string key = Convert.ToHexString(keyHash);
            if (!sessions.TryGetValue(key, out OperatorSession? stored) || stored.OperatorId != operatorId
                || stored.AuthEpoch != epoch || stored.AbsoluteExpiresAt <= now || stored.LastSeenAt.AddMinutes(30) <= now)
                return Task.FromResult<OperatorSession?>(null);
            OperatorSession updated = stored with { LastSeenAt = now, ProtectedTicket = stored.ProtectedTicket.ToArray() };
            sessions[key] = updated;
            return Task.FromResult<OperatorSession?>(updated);
        }
    }
    public Task RevokeAsync(byte[] keyHash, DateTimeOffset now, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (gate) sessions.Remove(Convert.ToHexString(keyHash));
        return Task.CompletedTask;
    }
    public Task PurgeAsync(DateTimeOffset now, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        lock (gate)
            foreach (string key in sessions.Where(p => p.Value.AbsoluteExpiresAt.AddHours(24) < now).Select(p => p.Key).ToArray())
                sessions.Remove(key);
        return Task.CompletedTask;
    }
}
