using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Evidence;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class EvidenceModel(OperationsEvidenceFacade evidence) : PageModel
{
    public Guid CharacterId { get; private set; }
    public WebEvidencePage? Evidence { get; private set; }
    public Failure? Error { get; private set; }
    public string From => Evidence?.FromUtc.UtcDateTime.ToString("O") ?? Request.Query["fromUtc"].ToString();
    public string To => Evidence?.ToUtc.UtcDateTime.ToString("O") ?? Request.Query["toUtc"].ToString();
    public string? NextLink { get; private set; }
    public bool SourceUnavailable => Evidence?.Coverage.Any(c => c.DetailCode == "ops.evidence.source_unavailable") == true;
    public static string CoverageDescription(EvidenceCoverage coverage) => coverage.DetailCode switch
    {
        "ops.evidence.source_unavailable" => "조회 실패. 다른 출처의 결과만 표시합니다.",
        "ops.evidence.actor_or_result_owner_only" => "실행자 또는 결과 소유자로 확인된 범위. 전체 소유 이력은 아닙니다.",
        "ops.evidence.actions_not_implemented" => "운영 조치 기능은 아직 구현되지 않았습니다.",
        "ops.evidence.query_logs_separately" => "기술 로그는 보존 기간을 지정해 별도로 조회하세요.",
        _ => "지정한 기간의 저장 기록을 조회했습니다. 전체 게임 변경 이력을 의미하지 않습니다."
    };
    public static string LogLink(EvidenceItem item)
    {
        string field = item.RequestId.HasValue ? "transactionRequestId" : "dungeonSessionId";
        return QueryHelpers.AddQueryString("/ops/diagnostics", new Dictionary<string,string?>
        {
            [field] = (item.RequestId ?? item.SessionId)?.ToString("D"),
            ["fromUtc"] = item.OccurredAtUtc.AddMinutes(-5).UtcDateTime.ToString("O"),
            ["toUtc"] = item.OccurredAtUtc.AddMinutes(5).UtcDateTime.ToString("O")
        });
    }
    public async Task OnGetAsync(string id)
    {
        if (!OperationsInput.Id(id, out Guid character)) { Error = Failures.InvalidQuery; Response.StatusCode = 400; return; }
        CharacterId = character;
        var result = await evidence.ReadAsync(HttpContext, character);
        if (!result.IsSuccess) { Error = result.Error; Response.StatusCode = OperationsHttp.Status(Error!); return; }
        Evidence = result.Value;
        if (Evidence.NextCursor is not null) NextLink = QueryHelpers.AddQueryString(Request.Path, new Dictionary<string,string?>
        { ["fromUtc"] = From, ["toUtc"] = To, ["limit"] = Request.Query["limit"].FirstOrDefault(), ["cursor"] = Evidence.NextCursor });
    }
}
