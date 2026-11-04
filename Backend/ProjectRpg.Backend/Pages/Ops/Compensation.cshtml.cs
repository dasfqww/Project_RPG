using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Compensation;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class CompensationModel(OperationsOptions options, ICompensationPolicy policy,
    GetCharacterSummary characters, OperationsCompensationFacade actions) : PageModel
{
    public CharacterHeader? Character { get; private set; }
    public WebActionPage? Actions { get; private set; }
    public IReadOnlyList<string> Definitions => policy.AllowedDefinitions;
    public Failure? Error { get; private set; }
    public string StorageKey => $"ops.compensation.{options.EnvironmentId}.{options.OperatorId}";
    public static string LogLink(OperationAction action) => QueryHelpers.AddQueryString("/ops/diagnostics", new Dictionary<string,string?>
    {
        ["operationRequestId"] = action.RequestId.ToString("D"),
        ["fromUtc"] = action.CommittedAtUtc.AddMinutes(-5).UtcDateTime.ToString("O"),
        ["toUtc"] = action.CommittedAtUtc.AddMinutes(5).UtcDateTime.ToString("O")
    });
    public async Task<IActionResult> OnGetAsync(string id)
    {
        if (!options.Compensation.Enabled) return NotFound();
        if (!OperationsInput.Id(id, out Guid characterId) || !OperationsInput.KnownQuery(HttpContext, "limit", "cursor")) return BadRequest();
        var character = await characters.ExecuteAsync(characterId, HttpContext.RequestAborted);
        if (!character.IsSuccess) { Error = character.Error; Response.StatusCode = OperationsHttp.Status(Error!); return Page(); }
        Character = character.Value;
        var recent = await actions.SearchAsync(HttpContext, characterId);
        if (!recent.IsSuccess) { Error = recent.Error; Response.StatusCode = OperationsHttp.Status(Error!); }
        else Actions = recent.Value;
        return Page();
    }
}
