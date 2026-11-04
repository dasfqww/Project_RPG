using Microsoft.AspNetCore.Antiforgery;
using Microsoft.AspNetCore.Authentication;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Web;

public static class OperationsEndpoints
{
    public static void Map(WebApplication app)
    {
        app.MapPost("/ops/auth/login", async (HttpContext context, IAntiforgery antiforgery) =>
        {
            await antiforgery.ValidateRequestAsync(context);
            IFormCollection form = await context.Request.ReadFormAsync(context.RequestAborted);
            string path = form["returnUrl"].FirstOrDefault() ?? "/ops";
            if (!SafeReturn(path)) return OperationsHttp.Error(context, Failures.InvalidQuery);
            return Results.Challenge(new AuthenticationProperties { RedirectUri = path }, [OperationsAuthentication.GitHubScheme]);
        }).AllowAnonymous();
        app.MapPost("/ops/auth/logout", async (HttpContext context, IAntiforgery antiforgery) =>
        {
            await antiforgery.ValidateRequestAsync(context);
            await context.SignOutAsync(OperationsAuthentication.CookieScheme);
            return Results.Redirect("/ops/login");
        }).RequireAuthorization(OperationsAuthentication.Policy);
        var api = app.MapGroup("/ops/api").RequireAuthorization(OperationsAuthentication.Policy);
        api.MapGet("/characters", async (HttpContext c, OperationsReadFacade q, OperationsOptions o, TimeProvider t) => Reply(c, await q.SearchAsync(c), o, t));
        api.MapGet("/characters/{id}", async (string id, HttpContext c, OperationsReadFacade q, OperationsOptions o, TimeProvider t) =>
            OperationsInput.Id(id, out Guid value) && OperationsInput.KnownQuery(c)
                ? Reply(c, await q.CharacterAsync(value, c.RequestAborted), o, t) : OperationsHttp.Error(c, Failures.InvalidQuery));
        api.MapGet("/characters/{id}/inventory", async (string id, HttpContext c, OperationsReadFacade q, OperationsOptions o, TimeProvider t) =>
            OperationsInput.Id(id, out Guid value) && OperationsInput.KnownQuery(c, "container", "limit", "cursor")
                ? Reply(c, await q.InventoryAsync(c, value), o, t) : OperationsHttp.Error(c, Failures.InvalidQuery));
        api.MapGet("/characters/{id}/wallet", async (string id, HttpContext c, OperationsReadFacade q, OperationsOptions o, TimeProvider t) =>
            OperationsInput.Id(id, out Guid value) && OperationsInput.KnownQuery(c)
                ? Reply(c, await q.WalletAsync(value, c.RequestAborted), o, t) : OperationsHttp.Error(c, Failures.InvalidQuery));
        api.MapGet("/characters/{id}/sessions", async (string id, HttpContext c, OperationsReadFacade q, OperationsOptions o, TimeProvider t) =>
            OperationsInput.Id(id, out Guid value) && OperationsInput.KnownQuery(c, "limit", "cursor")
                ? Reply(c, await q.SessionsAsync(c, value), o, t) : OperationsHttp.Error(c, Failures.InvalidQuery));
        api.MapGet("/logs", async (HttpContext c, OperationsReadFacade q, OperationsOptions o, TimeProvider t) => Reply(c, await q.LogsAsync(c), o, t));
    }
    private static IResult Reply<T>(HttpContext context, Result<T> result, OperationsOptions options, TimeProvider clock) where T : notnull =>
        result.IsSuccess ? OperationsHttp.Success(context, result.Value, options, clock) : OperationsHttp.Error(context, result.Error!);
    internal static bool SafeReturn(string value) => (value == "/ops" || value.StartsWith("/ops/", StringComparison.Ordinal))
        && !value.Contains('\\') && !value.Any(char.IsControl) && value.Length <= 512;
}
