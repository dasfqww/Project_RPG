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
            var input = await Body<CompensationIntent>(c, csrf);
            if (!input.IsSuccess) return OperationsHttp.Error(c, input.Error!);
            var result = await facade.PreviewAsync(input.Value, c.RequestAborted);
            return result.IsSuccess ? OperationsHttp.Success(c, result.Value, o, clock) : OperationsHttp.Error(c, result.Error!);
        });
        api.MapPost("/compensations", async (HttpContext c, IAntiforgery csrf, OperationsCompensationFacade facade, OperationsOptions o, TimeProvider clock) =>
        {
            var input = await Body<CompensationSubmission>(c, csrf);
            if (!input.IsSuccess) return OperationsHttp.Error(c, input.Error!);
            var result = await facade.CommitAsync(c, input.Value);
            return result.IsSuccess ? Results.Json(new { data = result.Value, meta = OperationsHttp.Meta(c, o, clock) }, OperationsHttp.Json,
                statusCode: result.Value.Disposition == "Committed" ? 201 : 200) : OperationsHttp.Error(c, result.Error!);
        });
        api.MapGet("/actions/{requestId}", async (string requestId, HttpContext c, OperationsCompensationFacade facade, OperationsOptions o, TimeProvider clock) =>
        {
            if (!OperationsInput.Id(requestId, out var id) || !OperationsInput.KnownQuery(c)) return OperationsHttp.Error(c, Failures.InvalidQuery);
            var result = await facade.FindAsync(id, c.RequestAborted);
            return result.IsSuccess ? OperationsHttp.Success(c, result.Value, o, clock) : OperationsHttp.Error(c, result.Error!);
        });
        api.MapGet("/actions", async (HttpContext c, OperationsCompensationFacade facade, OperationsOptions o, TimeProvider clock) =>
        {
            if (!OperationsInput.KnownQuery(c, "targetScope", "targetId", "limit", "cursor") || c.Request.Query["targetScope"] != "Character"
                || !OperationsInput.Id(c.Request.Query["targetId"].ToString(), out var id)) return OperationsHttp.Error(c, Failures.InvalidQuery);
            var result = await facade.SearchAsync(c, id);
            return result.IsSuccess ? OperationsHttp.Success(c, result.Value, o, clock) : OperationsHttp.Error(c, result.Error!);
        });
    }
    private static async Task<Result<T>> Body<T>(HttpContext context, IAntiforgery csrf) where T : class
    {
        if (!context.Request.HasJsonContentType() || !OperationsInput.KnownQuery(context)) return Result<T>.Fail(Failures.InvalidQuery);
        await csrf.ValidateRequestAsync(context);
        byte[] bytes = new byte[32 * 1024 + 1];
        int total = 0, read;
        while (total < bytes.Length && (read = await context.Request.Body.ReadAsync(bytes.AsMemory(total), context.RequestAborted)) > 0) total += read;
        if (total == bytes.Length) throw new BadHttpRequestException("Operations body limit exceeded.", 413);
        try
        {
            T? value = JsonSerializer.Deserialize<T>(bytes.AsSpan(0, total), OperationsHttp.Json);
            return value is null ? Result<T>.Fail(Failures.InvalidQuery) : Result<T>.Success(value);
        }
        catch (JsonException) { return Result<T>.Fail(Failures.InvalidQuery); }
    }
}
