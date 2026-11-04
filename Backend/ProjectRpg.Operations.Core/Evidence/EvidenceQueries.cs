using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Operations.Core.Evidence;

public enum EvidenceSourceKind { CurrencyReceipt, ItemReceipt, Settlement }
public enum EvidenceStatus { Delivered, Pending, Failed, Unknown }
public enum EvidenceLinkReason { DirectReceipt, SessionMembership, OwnerScope, TechnicalCorrelation }
public sealed record CurrencyEvidence(string CurrencyCode, string Scope, string OwnerId,
    long Delta, long PreviousBalance, long NewBalance, long Revision);
public sealed record EvidenceItem(EvidenceSourceKind SourceKind, Guid SourceKey, DateTimeOffset OccurredAtUtc,
    Guid CharacterId, string OwnerScope, string OwnerId, Guid? SessionId, Guid? RequestId,
    IReadOnlyList<Guid> ItemIds, EvidenceStatus Status, EvidenceLinkReason LinkReason, string Operation,
    IReadOnlyList<CurrencyEvidence> CurrencyChanges, string? StoredState = null, bool DetailsTruncated = false);
public sealed record EvidencePosition(PagePosition? CurrencyReceipt, PagePosition? ItemReceipt, PagePosition? Settlement)
{
    public PagePosition? For(EvidenceSourceKind source) => source switch
    { EvidenceSourceKind.CurrencyReceipt => CurrencyReceipt, EvidenceSourceKind.ItemReceipt => ItemReceipt, _ => Settlement };
    public bool IsValid => new[] { CurrencyReceipt, ItemReceipt, Settlement }.All(p => p is null || p.Id != Guid.Empty && p.Timestamp.HasValue);
}
public sealed record EvidenceQuery(Guid CharacterId, DateTimeOffset FromUtc, DateTimeOffset ToUtc, int Limit = 50, EvidencePosition? After = null)
{
    public bool IsValid => CharacterId != Guid.Empty && ToUtc > FromUtc && ToUtc - FromUtc <= TimeSpan.FromDays(30)
        && Limit is >= 1 and <= 200 && (After?.IsValid ?? true);
}
public sealed record EvidenceCoverage(string Source, LogCoverage Coverage, string DetailCode);
public sealed record EvidencePage(IReadOnlyList<EvidenceItem> Items, bool HasMore, EvidencePosition? Next,
    IReadOnlyList<EvidenceCoverage> Coverage);
public interface IEvidenceReadStore
{
    Task<Result<Page<EvidenceItem>>> ReadAsync(CharacterHeader target, EvidenceSourceKind source, EvidenceQuery query, CancellationToken ct);
    Task<Result<EvidenceItem>> FindAsync(CharacterHeader target, EvidenceSourceKind source, Guid key, CancellationToken ct);
}

public sealed class GetCharacterEvidence(IOperatorAccess access, ICharacterReadStore characters, IEvidenceReadStore evidence)
{
    public async Task<Result<EvidencePage>> ExecuteAsync(EvidenceQuery query, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.ReadEvidence, ct);
        if (!actor.IsSuccess) return Result<EvidencePage>.Fail(actor.Error!);
        if (!query.IsValid) return Result<EvidencePage>.Fail(Failures.InvalidQuery);
        var target = await characters.FindAsync(query.CharacterId, ct);
        if (!target.IsSuccess) return Result<EvidencePage>.Fail(target.Error!);
        List<EvidenceItem> rows = [];
        List<EvidenceCoverage> coverage = [];
        bool unavailable = false, more = false;
        int availableSources = 0;
        foreach (EvidenceSourceKind source in Enum.GetValues<EvidenceSourceKind>())
        {
            var result = await evidence.ReadAsync(target.Value, source, query, ct);
            if (!result.IsSuccess)
            {
                unavailable = true;
                coverage.Add(new(source.ToString(), LogCoverage.Unknown, "ops.evidence.source_unavailable"));
                continue;
            }
            rows.AddRange(result.Value.Items);
            availableSources++;
            more |= result.Value.HasMore;
            coverage.Add(new(source.ToString(), source == EvidenceSourceKind.ItemReceipt ? LogCoverage.Partial : LogCoverage.Complete,
                source == EvidenceSourceKind.ItemReceipt ? "ops.evidence.actor_or_result_owner_only" : "ops.evidence.stored_records_only"));
        }
        if (availableSources == 0) return Result<EvidencePage>.Fail(Failures.StorageUnavailable);
        var items = rows.OrderByDescending(e => e.OccurredAtUtc).ThenBy(e => e.SourceKind).ThenByDescending(e => e.SourceKey)
            .Take(query.Limit).ToArray();
        bool hasMore = !unavailable && (more || rows.Count > items.Length);
        PagePosition? Position(EvidenceSourceKind source)
        {
            var last = items.LastOrDefault(i => i.SourceKind == source);
            return last is null ? query.After?.For(source) : new(last.SourceKey, last.OccurredAtUtc);
        }
        coverage.Add(new("OperationsActions", LogCoverage.NotInstrumented, "ops.evidence.actions_not_implemented"));
        coverage.Add(new("TechnicalLogs", LogCoverage.Unknown, "ops.evidence.query_logs_separately"));
        return Result<EvidencePage>.Success(new(Array.AsReadOnly(items), hasMore,
            hasMore ? new(Position(EvidenceSourceKind.CurrencyReceipt), Position(EvidenceSourceKind.ItemReceipt), Position(EvidenceSourceKind.Settlement)) : null,
            coverage.AsReadOnly()));
    }

    public async Task<Result<EvidenceItem>> FindAsync(Guid characterId, EvidenceSourceKind source, Guid key, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.ReadEvidence, ct);
        if (!actor.IsSuccess) return Result<EvidenceItem>.Fail(actor.Error!);
        if (characterId == Guid.Empty || key == Guid.Empty || !Enum.IsDefined(source)) return Result<EvidenceItem>.Fail(Failures.InvalidQuery);
        var target = await characters.FindAsync(characterId, ct);
        return target.IsSuccess ? await evidence.FindAsync(target.Value, source, key, ct) : Result<EvidenceItem>.Fail(target.Error!);
    }
}
