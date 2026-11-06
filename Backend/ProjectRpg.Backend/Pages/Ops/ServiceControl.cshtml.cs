using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.ServiceControl;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class ServiceControlModel(OperationsOptions options, OperationsServiceFacade service) : PageModel
{
    public ServiceSnapshot? Status { get; private set; }
    public string? Error { get; private set; }
    public string StorageKey => $"ops.service.{options.EnvironmentId}.{options.OperatorId}";
    public async Task<IActionResult> OnGetAsync()
    {
        if (!options.ServiceControlEnabled) return NotFound();
        if (!OperationsInput.KnownQuery(HttpContext)) return BadRequest();
        var result = await service.ReadAsync(HttpContext.RequestAborted);
        if (result.IsSuccess) Status = result.Value;
        else { Error = result.Error!.Code; Response.StatusCode = OperationsHttp.Status(result.Error!); }
        return Page();
    }
}
