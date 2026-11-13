using System.Threading.RateLimiting;

namespace ProjectRpg.Backend.Application;

public sealed class RidingRequestLimiter : IAsyncDisposable
{
    private readonly PartitionedRateLimiter<string> limiter = PartitionedRateLimiter.Create<string, string>(key =>
        RateLimitPartition.GetTokenBucketLimiter(key, _ => new TokenBucketRateLimiterOptions
        {
            TokenLimit = 10, TokensPerPeriod = key.StartsWith("write:", StringComparison.Ordinal) ? 2 : 5,
            ReplenishmentPeriod = TimeSpan.FromSeconds(1), QueueLimit = 0, AutoReplenishment = true
        }));
    public bool Acquire(string account, bool write)
    { using var lease = limiter.AttemptAcquire((write ? "write:" : "read:") + account); return lease.IsAcquired; }
    public ValueTask DisposeAsync() => limiter.DisposeAsync();
}
