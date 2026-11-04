using System.Text.Json;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class LocalJsonlOperationLogReader(OperationsLogStore store, OperationsOptions options) : IOperationLogReader
{
    public async Task<Result<OperationLogPage>> SearchAsync(OperationLogQuery query, CancellationToken ct)
    {
        if (query.SourceId != options.SourceId) return Result<OperationLogPage>.Fail(Failures.InvalidQuery);
        List<LogEvent> events = [];
        LogReadBudget budget = new();
        LogCoverage coverage = query.FromUtc < store.CoverageStart ? LogCoverage.Unknown : LogCoverage.Complete;
        if (store.Dropped > 0 || store.SinkErrors > 0) coverage = LogCoverage.Partial;
        LogTruncation? reason = null;
        List<LogFilePosition> positions;
        int index = query.After?.FileIndex ?? 0;
        ReverseJsonLines? lines = null;
        try
        {
            store.SaveCatalogue(false);
            if (store.Dropped > 0 || store.SinkErrors > 0) coverage = LogCoverage.Partial;
            OperationsLogStore.LogFileSnapshot[] available = store.Files();
            positions = query.After?.Files.ToList() ?? available.Select(f =>
                new LogFilePosition(f.Name, f.Length, f.Length, f.CreationTimeUtc.Ticks)).ToList();
            if (positions.Count > 30 || index < 0 || index > positions.Count
                || positions.Any(p => p.Length < 0 || p.NextOffset < 0 || p.NextOffset > p.Length
                    || Path.GetFileName(p.Key) != p.Key || !p.Key.StartsWith("ops-", StringComparison.Ordinal) || !p.Key.EndsWith(".jsonl", StringComparison.Ordinal)))
                return Result<OperationLogPage>.Fail(Failures.InvalidCursor);
            foreach (LogFilePosition position in positions)
                if (!available.Any(f => f.Name == position.Key && f.CreationTimeUtc.Ticks == position.CreationTicks && f.Length >= position.Length))
                    return Result<OperationLogPage>.Fail(Failures.CursorExpired);
            for (; index < positions.Count; index++)
            {
                LogFilePosition position = positions[index];
                string path = Path.Combine(store.Root, position.Key);
                if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                    return Result<OperationLogPage>.Fail(Failures.LogsUnavailable);
                await using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 65536, FileOptions.Asynchronous);
                lines = new(stream, position.NextOffset, budget);
                try
                {
                    while (lines.Offset > 0)
                    {
                        ct.ThrowIfCancellationRequested();
                        (byte[]? line, bool corrupt) = await lines.PreviousAsync(ct);
                        if (corrupt) { coverage = LogCoverage.Partial; reason = LogTruncation.CorruptRecord; continue; }
                        if (line is not { Length: > 0 }) continue;
                        LogEvent? item;
                        try
                        {
                            using JsonDocument envelope = JsonDocument.Parse(line);
                            if (!envelope.RootElement.TryGetProperty("Properties", out JsonElement properties)
                                || !properties.TryGetProperty("OpsEvent", out JsonElement value)) continue;
                            item = value.Deserialize<LogEvent>(OperationsHttp.Json);
                        }
                        catch (JsonException) { coverage = LogCoverage.Partial; reason = LogTruncation.CorruptRecord; continue; }
                        if (item is null || item.SchemaVersion != 1) { coverage = LogCoverage.Partial; reason = LogTruncation.CorruptRecord; continue; }
                        if (item.Environment != options.EnvironmentId || item.TimestampUtc < query.FromUtc || item.TimestampUtc >= query.ToUtc
                            || !Matches(item, query.Filters)) continue;
                        events.Add(item);
                        if (events.Count == query.Limit)
                        {
                            positions[index] = position with { NextOffset = lines.Offset };
                            return Result<OperationLogPage>.Success(new(events.AsReadOnly(), new(positions.AsReadOnly(), index),
                                LogCoverage.Partial, LogTruncation.ResultLimit, [options.SourceId], budget.Bytes, query.FromUtc, query.ToUtc));
                        }
                    }
                }
                catch (LogReadLimitException limit)
                {
                    positions[index] = position with { NextOffset = lines.Offset };
                    return Result<OperationLogPage>.Success(new(events.AsReadOnly(), new(positions.AsReadOnly(), index),
                        LogCoverage.Partial, limit.Timeout ? LogTruncation.Timeout : LogTruncation.ReadBudget,
                        [options.SourceId], budget.Bytes, query.FromUtc, query.ToUtc));
                }
            }
            return Result<OperationLogPage>.Success(new(events.AsReadOnly(), null, coverage, reason,
                [options.SourceId], budget.Bytes, query.FromUtc, query.ToUtc));
        }
        catch (FileNotFoundException) when (query.After is not null) { return Result<OperationLogPage>.Fail(Failures.CursorExpired); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        { return Result<OperationLogPage>.Fail(Failures.LogsUnavailable); }
    }
    private static bool Matches(LogEvent item, IReadOnlyDictionary<string,string> filters)
    {
        foreach ((string key, string expected) in filters)
        {
            string? actual = key switch
            {
                "eventName" => item.EventName, "level" => item.Level, "traceId" => item.TraceId,
                "httpRequestId" => item.HttpRequestId?.ToString("D"), "characterId" => item.CharacterId?.ToString("D"),
                "operationRequestId" => item.OperationRequestId?.ToString("D"), "dungeonSessionId" => item.DungeonSessionId?.ToString("D"),
                "transactionRequestId" => item.TransactionRequestId?.ToString("D"), "itemId" => item.ItemId?.ToString("D"), "caseId" => item.CaseId?.ToString("D"),
                _ => null
            };
            if (!string.Equals(actual, expected, StringComparison.Ordinal)) return false;
        }
        return true;
    }
}
