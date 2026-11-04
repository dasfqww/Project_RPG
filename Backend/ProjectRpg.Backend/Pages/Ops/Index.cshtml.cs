using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class IndexModel(OperationsReadFacade reads) : PageModel
{
    public string Kind => Request.Query["kind"].FirstOrDefault() ?? "SteamId";
    public string Value => Request.Query["value"].ToString();
    public WebPage<CharacterHeader>? Results { get; private set; }
    public Failure? Error { get; private set; }
    public string NextLink => QueryHelpers.AddQueryString("/ops", new Dictionary<string,string?>
        { ["kind"] = Kind, ["value"] = Value, ["limit"] = Request.Query["limit"].FirstOrDefault(), ["cursor"] = Results?.NextCursor });
    public async Task OnGetAsync()
    {
        if (Request.Query.Count == 0) return;
        var result = await reads.SearchAsync(HttpContext);
        if (result.IsSuccess) Results = result.Value;
        else { Error = result.Error; Response.StatusCode = OperationsHttp.Status(Error!); }
    }
}
