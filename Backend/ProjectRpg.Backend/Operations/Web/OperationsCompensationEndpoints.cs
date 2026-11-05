using System.Text.Json;
using Microsoft.AspNetCore.Antiforgery;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Compensation;

namespace ProjectRpg.Backend.Operations.Web;

internal static class OperationsCompensationEndpoints
{
    internal static void Map(WebApplication app)
    {
        if (!app.Services.GetRequiredService<OperationsOptions>().Compensation.Enabled) return;
        var api = app.MapGroup("/ops/api").RequireAuthorization(OperationsAuthentication.Policy);
        api.MapPost("/compensations/preview", async (HttpContext c, IAntiforgery csrf, OperationsCompensationFacade facade, OperationsOptions o, TimeProvider clock) =>
        {
            var input = await OperationsCommandInput.ReadAsync<CompensationIntent>(c, csrf);
            if (!input.IsSuccess) return OperationsHttp.Error(c, input.Error!);
            var result = await facade.PreviewAsync(input.Value, c.RequestAborted);
            return result.IsSuccess ? OperationsHttp.Success(c, result.Value, o, clock) : OperationsHttp.Error(c, result.Error!);
        });
        api.MapPost("/compensations", async (HttpContext c, IAntiforgery csrf, OperationsCompensationFacade facade, OperationsOptions o, TimeProvider clock) =>
        {
            var input = await OperationsCommandInput.ReadAsync<CompensationSubmission>(c, csrf);
            if (!input.IsSuccess) return OperationsHttp.Error(c, input.Error!);
            var result = await facade.CommitAsync(c, input.Value);
            return result.IsSuccess ? Results.Json(new { data = result.Value, meta = OperationsHttp.Meta(c, o, clock) }, OperationsHttp.Json,
                statusCode: result.Value.Disposition == "Committed" ? 201 : 200) : OperationsHttp.Error(c, result.Error!);
        });

    }

}
