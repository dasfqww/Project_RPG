using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Authentication;

public sealed class ApiAuthenticationMiddleware(RequestDelegate next)
{
    public const string PrincipalItemKey = "ProjectRpg.AuthenticatedPrincipal";

    public async Task InvokeAsync(HttpContext context, BearerAuthenticator authenticator)
    {
        if (!context.Request.Path.StartsWithSegments("/api")
            || context.Request.Path.Equals("/api/auth/steam-ticket", StringComparison.OrdinalIgnoreCase))
        {
            await next(context);
            return;
        }

        string authorization = context.Request.Headers.Authorization.ToString();
        const string bearerPrefix = "Bearer ";
        if (!authorization.StartsWith(bearerPrefix, StringComparison.OrdinalIgnoreCase))
        {
            context.Response.StatusCode = StatusCodes.Status401Unauthorized;
            return;
        }

        string token = authorization[bearerPrefix.Length..].Trim();
        AuthenticatedPrincipal? principal = await authenticator.AuthenticateAsync(
            token,
            context.RequestAborted);
        if (principal is null)
        {
            context.Response.StatusCode = StatusCodes.Status401Unauthorized;
            return;
        }

        context.Items[PrincipalItemKey] = principal;
        await next(context);
    }
}

public static class HttpContextPrincipalExtensions
{
    public static AuthenticatedPrincipal GetProjectRpgPrincipal(this HttpContext context)
    {
        return context.Items.TryGetValue(
                ApiAuthenticationMiddleware.PrincipalItemKey,
                out object? value)
            && value is AuthenticatedPrincipal principal
                ? principal
                : throw new InvalidOperationException("The request is not authenticated.");
    }
}
