using System.Threading.RateLimiting;

namespace ProjectRpg.Backend.Application;

// Single-backend protection, in addition to the existing ingress/player budgets. Not a distributed quota.
public sealed class TradeRequestLimiter : IAsyncDisposable
{
    private readonly PartitionedRateLimiter<string> limiter = PartitionedRateLimiter.Create<string, string>(key =>
        RateLimitPartition.GetFixedWindowLimiter(key, _ => new FixedWindowRateLimiterOptions
        {
            PermitLimit = key.StartsWith("invite:", StringComparison.Ordinal) ? 5
                : key.StartsWith("receive:", StringComparison.Ordinal) ? 10 : 120,
            Window = TimeSpan.FromMinutes(1), QueueLimit = 0, AutoReplenishment = true
        }));

    public bool Acquire(string account, bool invite, string? recipient)
    {
        using var actor = limiter.AttemptAcquire((invite ? "invite:" : "command:") + account);
        if (!actor.IsAcquired) return false;
        if (recipient is null) return true;
        using var target = limiter.AttemptAcquire("receive:" + recipient);
        return target.IsAcquired;
    }

    public ValueTask DisposeAsync() => limiter.DisposeAsync();
}
