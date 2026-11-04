using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Sessions;

public enum LeaseTiming { WithinObservedLease, ObservedExpired, LeaseMissing }
public sealed record SettlementSummary(string StoredState, int AttemptCount, DateTimeOffset NextAttemptAtUtc,
    DateTimeOffset? CompletedAtUtc, string? ErrorCode);
public sealed record SessionEvidence(Guid DungeonSessionId, string DungeonId, string Difficulty, string StoredState,
    DateTimeOffset UpdatedAtUtc, DateTimeOffset SessionExpiresAtUtc, DateTimeOffset? MemberLeaseExpiresAtUtc,
    DateTimeOffset? CharacterLeaseExpiresAtUtc, LeaseTiming Timing, SettlementSummary? Settlement)
{
    public static LeaseTiming Observe(DateTimeOffset now, DateTimeOffset sessionExpiry,
        DateTimeOffset? memberExpiry, DateTimeOffset? characterExpiry) =>
        !memberExpiry.HasValue || !characterExpiry.HasValue ? LeaseTiming.LeaseMissing :
        sessionExpiry <= now || memberExpiry <= now || characterExpiry <= now
            ? LeaseTiming.ObservedExpired : LeaseTiming.WithinObservedLease;
}
public interface ISessionEvidenceReadStore
{
    Task<Result<Page<SessionEvidence>>> ReadAsync(Guid characterId, PageRequest page, CancellationToken cancellationToken);
}
public sealed class GetSessionEvidence(IOperatorAccess access, ISessionEvidenceReadStore store)
{
    public async Task<Result<Page<SessionEvidence>>> ExecuteAsync(Guid id, PageRequest page, CancellationToken ct)
    {
        Result<OperatorActor> actor = await access.AuthorizeAsync(OpsCapability.ReadCharacter, ct);
        if (!actor.IsSuccess) return Result<Page<SessionEvidence>>.Fail(actor.Error!);
        if (id == Guid.Empty || !page.IsValid(50) || (page.After is not null && !page.After.Timestamp.HasValue))
            return Result<Page<SessionEvidence>>.Fail(Failures.InvalidQuery);
        return await store.ReadAsync(id, page, ct);
    }
}
