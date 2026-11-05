using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Restrictions;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class RestrictionsModel(OperationsOptions options,OperationsRestrictionFacade restrictions):PageModel
{
    public RestrictionSnapshot? Status {get;private set;}
    public WebRestrictionPage? History {get;private set;}
    public Failure? Error {get;private set;}
    public string StorageKey=>$"ops.restrictions.{options.EnvironmentId}.{options.OperatorId}";
    public static string LogLink(RestrictionReceipt receipt)=>Microsoft.AspNetCore.WebUtilities.QueryHelpers.AddQueryString("/ops/diagnostics",new Dictionary<string,string?>
    {
        ["operationRequestId"]=receipt.RequestId.ToString("D"),
        ["fromUtc"]=receipt.CommittedAtUtc.AddMinutes(-5).UtcDateTime.ToString("O"),
        ["toUtc"]=receipt.CommittedAtUtc.AddMinutes(5).UtcDateTime.ToString("O")
    });
    public async Task<IActionResult> OnGetAsync(string account)
    {
        if(!options.RestrictionsEnabled)return NotFound();
        if(!OperationsInput.KnownQuery(HttpContext,"limit","cursor"))return BadRequest();
        var result=await restrictions.FindAsync(account,Request.HttpContext.RequestAborted);
        if(!result.IsSuccess){Error=result.Error;Response.StatusCode=OperationsHttp.Status(Error!);return Page();}
        Status=result.Value;
        var history=await restrictions.SearchAsync(HttpContext,account);
        if(!history.IsSuccess){Error=history.Error;Response.StatusCode=OperationsHttp.Status(Error!);}else History=history.Value;
        return Page();
    }
}
