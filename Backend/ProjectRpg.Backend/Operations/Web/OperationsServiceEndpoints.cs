using Microsoft.AspNetCore.Antiforgery;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.ServiceControl;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record ChangeServiceControlRequest(Guid RequestId, long ExpectedRevision, string Mode, string NoticeText,
    DateTimeOffset? NoticeStartsAtUtc, DateTimeOffset? NoticeEndsAtUtc, string Reason)
{
    public ServiceIntent ToIntent() => new(RequestId, ExpectedRevision, Mode, NoticeText, NoticeStartsAtUtc, NoticeEndsAtUtc, Reason);
}
internal static class OperationsServiceEndpoints
{
    internal static void Map(WebApplication app)
    {
        if (!app.Services.GetRequiredService<OperationsOptions>().ServiceControlEnabled) return;
        var api = app.MapGroup("/ops/api/service-control").RequireAuthorization(OperationsAuthentication.Policy);
        api.MapGet("", async (HttpContext c, OperationsServiceFacade facade, OperationsOptions o, TimeProvider clock) =>
        {
            if (!OperationsInput.KnownQuery(c)) return OperationsHttp.Error(c, Failures.InvalidQuery);
            var result = await facade.ReadAsync(c.RequestAborted);
            return result.IsSuccess ? OperationsHttp.Success(c, result.Value, o, clock) : OperationsHttp.Error(c, result.Error!);
        });
        api.MapPut("", async (HttpContext c, IAntiforgery csrf, OperationsServiceFacade facade, OperationsOptions o, TimeProvider clock) =>
        {
            var input = await OperationsCommandInput.ReadAsync<ChangeServiceControlRequest>(c, csrf);
            if (!input.IsSuccess) return OperationsHttp.Error(c, input.Error!);
            var result = await facade.CommitAsync(c, input.Value.ToIntent());
            return result.IsSuccess ? Results.Json(new { data = result.Value, meta = OperationsHttp.Meta(c, o, clock) }, OperationsHttp.Json,
                statusCode: result.Value.Disposition == "Committed" ? 201 : 200) : OperationsHttp.Error(c, result.Error!);
        });
    }
}
