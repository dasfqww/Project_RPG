using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class DungeonAbandonmentApiEndpoints
{
    public static void MapDungeonAbandonmentApi(this WebApplication app)
    {
        // Deliberately separate from Waiting-only DELETE and local disconnect.
        // No account/owner supplied by the body can grant authority.
        app.MapPost("/api/dungeon-sessions/{dungeonSessionId:guid}/members/{characterId:guid}/abandon",
            async (Guid dungeonSessionId, Guid characterId, HttpContext context,
                IGameRepository repository, TimeProvider timeProvider, CancellationToken cancellationToken) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
                if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                string? owner = await repository.GetCharacterOwnerAsync(characterId, cancellationToken);
                if (owner is null) return Results.NotFound(new { error = "character_not_found" });
                if (!string.Equals(owner, principal.SteamId, StringComparison.Ordinal))
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                try
                {
                    DungeonAbandonment receipt = await repository.AbandonDungeonSessionAsync(
                        dungeonSessionId, principal.SteamId, characterId, timeProvider.GetUtcNow(), cancellationToken);
                    return Results.Ok(new DungeonAbandonmentResponse(receipt.DungeonSessionId,
                        receipt.CharacterId, receipt.AbandonedAt, receipt.SessionStateAtAbandonment.ToString(), true));
                }
                catch (DungeonSessionNotFoundException)
                {
                    return Results.NotFound(new { error = "dungeon_session_not_found" });
                }
                catch (DungeonSessionMembershipException)
                {
                    return Results.Json(new { error = "dungeon_membership_mismatch" }, statusCode: StatusCodes.Status403Forbidden);
                }
                catch (DungeonSessionStateConflictException)
                {
                    return Results.Conflict(new { error = "dungeon_abandonment_not_available" });
                }
            });
    }
}
