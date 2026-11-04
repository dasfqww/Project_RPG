using System.Globalization;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Api;

public sealed record NarrativeChoiceRequest(Guid RequestId, string ExpectedRevision, int CatalogVersion,
    string DialogueId, string ChoiceId);

public static class NarrativeApiEndpoints
{
    public static void MapNarrativeApi(this WebApplication app)
    {
        app.MapGet("/api/characters/{characterId:guid}/narrative", async (
            Guid characterId, HttpContext context, NarrativeCoordinator coordinator, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            try { return Results.Ok(ToResponse(await coordinator.LoadAsync(characterId, principal.SteamId, cancellationToken))); }
            catch (NarrativeConflictException error) { return Failure(error); }
        });
        app.MapPost("/api/characters/{characterId:guid}/narrative/choices", async (
            Guid characterId, NarrativeChoiceRequest request, HttpContext context,
            NarrativeCoordinator coordinator, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (!long.TryParse(request.ExpectedRevision, NumberStyles.None, CultureInfo.InvariantCulture, out long revision))
                return Results.BadRequest(new { error = "invalid_narrative_revision" });
            try
            {
                NarrativeCommand command = new(request.RequestId, revision, request.CatalogVersion, request.DialogueId, request.ChoiceId);
                return Results.Ok(ToResponse(await coordinator.ChooseAsync(characterId, principal.SteamId, command, cancellationToken)));
            }
            catch (NarrativeConflictException error) { return Failure(error); }
        });
    }

    private static IResult Failure(NarrativeConflictException error) => error.Message switch
    {
        "character_not_owned" => Results.StatusCode(StatusCodes.Status403Forbidden),
        "invalid_narrative_command" => Results.BadRequest(new { error = error.Message }),
        _ => Results.Conflict(new { error = error.Message })
    };

    // Revisions are decimal strings across the UE/JSON boundary; never float-rounded.
    private static object ToResponse(NarrativeView view) => new
    {
        view.CatalogVersion, view.State.CharacterId, view.State.SchemaVersion,
        Revision = view.State.Revision.ToString(CultureInfo.InvariantCulture),
        Quests = view.Journal,
        view.State.CompletedChapters, view.State.StoryUnlocks, view.AvailableDialogues
    };
}
