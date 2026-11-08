using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class DiagnosticsModel(OperationsReadFacade reads, OperationsOptions options) : PageModel
{
    public IReadOnlyList<LogIdentifierField> IdentifierFields => OperationsLogNavigation.IdentifierFields;
    public IEnumerable<string> EventNames => OperationLogQuery.EventNames.Order(StringComparer.Ordinal);
    public WebLogPage? Logs { get; private set; }
    public Failure? Error { get; private set; }
    public string From => Logs?.SearchedFromUtc.UtcDateTime.ToString("O") ?? Request.Query["fromUtc"].ToString();
    public string To => Logs?.SearchedToUtc.UtcDateTime.ToString("O") ?? Request.Query["toUtc"].ToString();
    public string? NextLink { get; private set; }
    public string SourceId => Logs is { SearchedSources.Count: 1 } ? Logs.SearchedSources[0] : options.SourceId;
    public IReadOnlyList<LogIdentifierLink> Connections(LogEvent entry) => Logs is null ? [] : OperationsLogNavigation.Connections(entry,Logs);
    public async Task OnGetAsync()
    {
        if (Request.Query.Count == 0) return;
        var result = await reads.LogsAsync(HttpContext);
        if (!result.IsSuccess) { Error = result.Error; Response.StatusCode = OperationsHttp.Status(Error!); return; }
        Logs = result.Value;
        if (Logs.NextCursor is not null)
        {
            var parameters = Request.Query.ToDictionary(p => p.Key, p => (string?)p.Value.ToString(), StringComparer.Ordinal);
            parameters["fromUtc"] = From; parameters["toUtc"] = To; parameters["cursor"] = Logs.NextCursor;
            NextLink = QueryHelpers.AddQueryString("/ops/diagnostics", parameters);
        }
    }
}
