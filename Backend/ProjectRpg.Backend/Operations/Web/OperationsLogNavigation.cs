using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record LogIdentifierField(string Key, string Label, int MaximumLength);
public sealed record LogIdentifierLink(string Key, string Label, string Value, string Url);

/// <summary>Presentation-only links into the authorized log search; no data access or inferred relationships.</summary>
public static class OperationsLogNavigation
{
    public static IReadOnlyList<LogIdentifierField> IdentifierFields { get; } = Array.AsReadOnly(new[]
    {
        new LogIdentifierField("httpRequestId","HTTP 요청 번호",36),
        new LogIdentifierField("traceId","Trace ID",32),
        new LogIdentifierField("operationRequestId","운영 요청 ID",36),
        new LogIdentifierField("caseId","조사 사건 ID",36),
        new LogIdentifierField("transactionRequestId","거래 요청 ID",36),
        new LogIdentifierField("characterId","캐릭터 ID",36),
        new LogIdentifierField("dungeonSessionId","던전 세션 ID",36),
        new LogIdentifierField("itemId","아이템 ID",36)
    });

    public static IReadOnlyList<LogIdentifierLink> Connections(LogEvent entry, WebLogPage page)
    {
        if (page.SearchedSources.Count != 1) return [];
        List<LogIdentifierLink> links = [];
        foreach (var field in IdentifierFields)
        {
            string? value = field.Key switch
            {
                "httpRequestId" => entry.HttpRequestId?.ToString("D"), "traceId" => entry.TraceId,
                "operationRequestId" => entry.OperationRequestId?.ToString("D"), "caseId" => entry.CaseId?.ToString("D"),
                "transactionRequestId" => entry.TransactionRequestId?.ToString("D"), "characterId" => entry.CharacterId?.ToString("D"),
                "dungeonSessionId" => entry.DungeonSessionId?.ToString("D"), "itemId" => entry.ItemId?.ToString("D"),
                _ => null
            };
            if (value is null) continue;
            var query = new OperationLogQuery(page.SearchedFromUtc,page.SearchedToUtc,page.SearchedSources[0],
                new Dictionary<string,string> { [field.Key] = value });
            if (!query.IsValid) continue;
            // Start a fresh search for this identity. Copying the old AND filters/cursor hides related attempts.
            string url = QueryHelpers.AddQueryString("/ops/diagnostics",new Dictionary<string,string?>
            {
                [field.Key] = value, ["sourceId"] = query.SourceId,
                ["fromUtc"] = query.FromUtc.UtcDateTime.ToString("O"), ["toUtc"] = query.ToUtc.UtcDateTime.ToString("O")
            });
            links.Add(new(field.Key,field.Label,value,url));
        }
        return links.AsReadOnly();
    }
}
