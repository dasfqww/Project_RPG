using System.Collections.Frozen;
using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Diagnostics;

public enum LogCoverage { Complete, Partial, Expired, NotInstrumented, Unknown }
public enum LogTruncation { ResultLimit, ReadBudget, Timeout, CorruptRecord, SourceMissing }
public sealed record LogEvent(int SchemaVersion, Guid EventId, string EventName, DateTimeOffset TimestampUtc,
    string Level, string Environment, string Service, string InstanceId, string BuildVersion,
    Guid? HttpRequestId, string? TraceId, string? SpanId, string? ParentSpanId, string OperationName,
    double? DurationMs, string? OperatorId, Guid? CharacterId, Guid? RosterId, Guid? DungeonSessionId,
    Guid? OperationRequestId, Guid? TransactionRequestId, Guid? ItemId, Guid? CaseId,
    string Outcome, string? ErrorCode, int? HttpStatus, string LoggerCategory, bool DetailsTruncated = false,
    Guid? TradeId = null, Guid? TradeCommandRequestId = null);
public sealed record LogFilePosition(string Key, long Length, long NextOffset, long CreationTicks);
public sealed record LogPosition(IReadOnlyList<LogFilePosition> Files, int FileIndex);
public sealed record OperationLogQuery(DateTimeOffset FromUtc, DateTimeOffset ToUtc, string SourceId,
    IReadOnlyDictionary<string, string> Filters, int Limit = 100, LogPosition? After = null)
{
    public static readonly IReadOnlySet<string> IdFields = new HashSet<string>(StringComparer.Ordinal)
    { "httpRequestId", "traceId", "operationRequestId", "characterId", "dungeonSessionId", "transactionRequestId", "itemId", "caseId",
      "tradeId", "tradeCommandRequestId" }.ToFrozenSet(StringComparer.Ordinal);
    public static readonly IReadOnlySet<string> EventNames = new HashSet<string>(StringComparer.Ordinal)
    { "ops.http.started", "ops.http.completed", "ops.access.denied", "ops.character.query.completed",
      "ops.character.query.failed", "ops.storage.query.completed", "ops.storage.query.failed",
      "ops.logs.search.completed", "ops.logs.search.failed", "ops.evidence.query.completed", "ops.evidence.query.failed",
      "ops.command.started", "ops.command.committed", "ops.command.replayed", "ops.command.rejected", "ops.command.unknown",
      "game.reward.receipt.committed", "game.settlement.attempt.started", "game.settlement.attempt.completed",
      "game.settlement.attempt.failed", "game.settlement.attempt.requeued", "game.http.started", "game.http.completed",
      "game.transaction.receipt.committed", "game.transaction.receipt.replayed", "game.trade.command.observed",
      "game.storage.returned", "game.storage.failed", "game.storage.canceled" }.ToFrozenSet(StringComparer.Ordinal);
    public bool IsValid => ToUtc > FromUtc && ToUtc - FromUtc <= TimeSpan.FromHours(24)
        && Limit is >= 1 and <= 200 && SourceId is { Length: >= 1 and <= 64 }
        && Filters.Count is >= 1 and <= 10 && Filters.Any(p => IdFields.Contains(p.Key) || p.Key == "eventName")
        && Filters.All(p => p.Value is { Length: >= 1 and <= 128 } && (p.Key switch
        {
            "traceId" => p.Value.Length == 32 && p.Value.Any(c => c != '0') && p.Value.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f'),
            "eventName" => EventNames.Contains(p.Value),
            "level" => p.Value is "Information" or "Warning" or "Error" or "Critical" or "Debug" or "Trace",
            _ => IdFields.Contains(p.Key) && CharacterLookup.ValidGuid(p.Value, out _)
        }));
}
public sealed record OperationLogPage(IReadOnlyList<LogEvent> Events, LogPosition? Next, LogCoverage Coverage,
    LogTruncation? TruncationReason, IReadOnlyList<string> SearchedSources, long ScannedBytes,
    DateTimeOffset SearchedFromUtc, DateTimeOffset SearchedToUtc);
public interface IOperationLogReader
{
    Task<Result<OperationLogPage>> SearchAsync(OperationLogQuery query, CancellationToken cancellationToken);
}
public sealed class SearchOperationLogs(IOperatorAccess access, IOperationLogReader reader)
{
    public async Task<Result<OperationLogPage>> ExecuteAsync(OperationLogQuery query, CancellationToken ct)
    {
        Result<OperatorActor> actor = await access.AuthorizeAsync(OpsCapability.ReadDiagnostics, ct);
        if (!actor.IsSuccess) return Result<OperationLogPage>.Fail(actor.Error!);
        if (!query.IsValid) return Result<OperationLogPage>.Fail(Failures.InvalidQuery);
        return await reader.SearchAsync(query, ct);
    }
}
