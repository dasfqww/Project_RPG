using System.Globalization;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Api;

public sealed record NarrativeStorySkipRequest(Guid RequestId, string ExpectedRevision, int CatalogVersion,
    string ChapterId, string PreviewHash, bool Confirmed);

public static class NarrativeStorySkipApiEndpoints
{
    public static void MapNarrativeStorySkipApi(this WebApplication app)
    {
        app.MapGet("/api/characters/{characterId:guid}/narrative/story-skip-preview", async (
            Guid characterId, string chapterId, HttpContext context,
            [Microsoft.AspNetCore.Mvc.FromServices] NarrativeStorySkip skip, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null) return Results.StatusCode(403);
            try { return Results.Ok(await skip.PreviewAsync(characterId, principal.SteamId, chapterId, ct)); }
            catch (NarrativeConflictException e) { return NarrativeApiEndpoints.Failure(e); }
        });
        app.MapPost("/api/characters/{characterId:guid}/narrative/story-skips", async (
            Guid characterId, NarrativeStorySkipRequest request, HttpContext context,
            [Microsoft.AspNetCore.Mvc.FromServices] NarrativeStorySkip skip, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null) return Results.StatusCode(403);
            if (!long.TryParse(request.ExpectedRevision, NumberStyles.None, CultureInfo.InvariantCulture, out long revision))
                return Results.BadRequest(new { error = "invalid_story_skip_command" });
            try
            {
                var result = await skip.ApplyAsync(characterId, principal.SteamId, new(request.RequestId, revision,
                    request.CatalogVersion, request.ChapterId, request.PreviewHash, request.Confirmed), ct);
                return Results.Ok(NarrativeApiEndpoints.ToResponse(result));
            }
            catch (NarrativeConflictException e) { return NarrativeApiEndpoints.Failure(e); }
        });
    }
}
