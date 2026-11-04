using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Evidence;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record WebEvidencePage(IReadOnlyList<EvidenceItem> Items, string? NextCursor, bool HasMore,
    IReadOnlyList<EvidenceCoverage> Coverage, DateTimeOffset FromUtc, DateTimeOffset ToUtc);
public sealed class OperationsEvidenceFacade(GetCharacterEvidence evidence, OperationsCursor cursors, OperationsOptions options, TimeProvider clock)
{
    public async Task<Result<WebEvidencePage>> ReadAsync(HttpContext context, Guid id)
    {
        context.Items["Ops.QueryKind"] = "Evidence";
        context.Items["Ops.CharacterId"] = id;
        DateTimeOffset to = clock.GetUtcNow(), from = to.AddDays(-7);
        if (!OperationsInput.KnownQuery(context, "fromUtc", "toUtc", "limit", "cursor")
            || !OperationsInput.Limit(context, 50, 200, out int limit)
            || !Date(context.Request.Query["fromUtc"], ref from) || !Date(context.Request.Query["toUtc"], ref to))
            return Result<WebEvidencePage>.Fail(Failures.InvalidQuery);
        string key = $"evidence\n{id:D}\n{from:O}\n{to:O}\n{limit}";
        var state = cursors.Read<EvidencePosition>(context.Request.Query["cursor"], options.OperatorId, key);
        if (!state.IsSuccess) return Result<WebEvidencePage>.Fail(state.Error!);
        var result = await evidence.ExecuteAsync(new(id, from, to, limit, state.Value.Position), context.RequestAborted);
        if (!result.IsSuccess) return Result<WebEvidencePage>.Fail(result.Error!);
        var page = result.Value;
        if (options.Compensation.Enabled) page = page with { Coverage = page.Coverage.Select(c => c.Source == "OperationsActions"
            ? new EvidenceCoverage(c.Source, ProjectRpg.Operations.Core.Diagnostics.LogCoverage.Unknown, "ops.evidence.query_actions_separately") : c).ToArray() };
        return Result<WebEvidencePage>.Success(new(page.Items, cursors.Write(page.Next, state.Value, options.OperatorId, key), page.HasMore, page.Coverage, from, to));
    }
    public Task<Result<EvidenceItem>> FindAsync(HttpContext context, Guid id, EvidenceSourceKind source, Guid key)
    {
        context.Items["Ops.QueryKind"] = "Evidence";
        context.Items["Ops.CharacterId"] = id;
        return evidence.FindAsync(id, source, key, context.RequestAborted);
    }
    public static bool Kind(string raw, out EvidenceSourceKind kind) => Enum.TryParse(raw, out kind) && Enum.IsDefined(kind) && kind.ToString() == raw;
    private static bool Date(string? raw, ref DateTimeOffset value)
    {
        if (string.IsNullOrEmpty(raw)) return true;
        if (!raw.EndsWith('Z') || !DateTimeOffset.TryParse(raw, System.Globalization.CultureInfo.InvariantCulture,
            System.Globalization.DateTimeStyles.RoundtripKind, out var parsed)) return false;
        value = parsed.ToUniversalTime();
        return true;
    }
}
