using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class PlayerLogoutApiEndpoints
{
    public static void MapPlayerLogoutApi(this WebApplication app)
    {
        app.MapPost("/api/auth/logout", async (HttpContext context, IGameRepository repository,
            AccessTokenService tokens, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            // The authentication middleware validated this header. Never accept
            // an account, session ID or target token supplied in a request body.
            string token = context.Request.Headers.Authorization.ToString()["Bearer ".Length..].Trim();
            await repository.RevokeSessionAsync(tokens.Hash(token), principal.SteamId, cancellationToken);
            return Results.NoContent();
        });
    }
}
