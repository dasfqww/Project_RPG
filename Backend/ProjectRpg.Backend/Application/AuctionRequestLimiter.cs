using System.Threading.RateLimiting;

namespace ProjectRpg.Backend.Application;

public sealed class AuctionRequestLimiter : IAsyncDisposable
{
    private readonly PartitionedRateLimiter<string> limiter = PartitionedRateLimiter.Create<string, string>(key =>
        RateLimitPartition.GetFixedWindowLimiter(key, _ => new FixedWindowRateLimiterOptions
        { PermitLimit = key.StartsWith("search:", StringComparison.Ordinal) ? 30 : 120, Window = TimeSpan.FromMinutes(1), QueueLimit = 0, AutoReplenishment = true }));
    public bool Acquire(string account, bool search)
    { using var lease = limiter.AttemptAcquire((search ? "search:" : "command:") + account); return lease.IsAcquired; }
    public ValueTask DisposeAsync() => limiter.DisposeAsync();
}
