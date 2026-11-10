using System.Globalization;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Api;

public sealed record NarrativeChoiceRequest(Guid RequestId, string ExpectedRevision, int CatalogVersion,
    string DialogueId, string ChoiceId);
public sealed record NarrativeDungeonObjectiveRequest(Guid RequestId, string ExpectedRevision, int CatalogVersion,
    Guid DungeonSessionId, string QuestId, string ObjectiveId);
public sealed record NarrativeInteractionRequest(Guid EventId, string TargetId, string ConnectionGeneration, DateTimeOffset ObservedAtUtc);
public sealed record NarrativeKillRequest(Guid EventId, string EnemyId, string ConnectionGeneration, DateTimeOffset ObservedAtUtc);

public static class NarrativeApiEndpoints
{
    public static void MapNarrativeApi(this WebApplication app)
    {
        app.MapGet("/api/characters/{characterId:guid}/narrative/account-history", async (
            Guid characterId, HttpContext context,
            [Microsoft.AspNetCore.Mvc.FromServices] NarrativeAccountHistory history, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            try { return Results.Ok(await history.LoadAsync(characterId, principal.SteamId, ct)); }
            catch (NarrativeConflictException error) { return Failure(error); }
        });
        app.MapPost("/api/dungeon-sessions/{sessionId:guid}/narrative/kills/{characterId:guid}", async (
            Guid sessionId, Guid characterId, NarrativeKillRequest request, HttpContext context,
            [Microsoft.AspNetCore.Mvc.FromServices] NarrativeKillCoordinator coordinator, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var principal = context.GetProjectRpgPrincipal();
            if (!principal.IsGameServer || principal.IsSecurityTelemetryOnly || principal.ServerId is null
                || principal.DungeonSessionId != sessionId) return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (!long.TryParse(request.ConnectionGeneration, NumberStyles.None, CultureInfo.InvariantCulture, out long generation))
                return Results.BadRequest(new { error = "invalid_kill_event" });
            try
            {
                var result = await coordinator.RecordAsync(sessionId, characterId, principal.ServerId,
                    new(request.EventId, request.EnemyId, generation, request.ObservedAtUtc), ct);
                return Results.Ok(new { result.EventId, result.CharacterId,
                    Revision = result.Revision.ToString(CultureInfo.InvariantCulture), result.AppliedObjectives });
            }
            catch (NarrativeConflictException error) { return Failure(error); }
        });
        app.MapPost("/api/dungeon-sessions/{sessionId:guid}/narrative/interactions/{characterId:guid}", async (
            Guid sessionId, Guid characterId, NarrativeInteractionRequest request, HttpContext context,
            [Microsoft.AspNetCore.Mvc.FromServices] NarrativeInteractionCoordinator coordinator, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var principal = context.GetProjectRpgPrincipal();
            if (!principal.IsGameServer || principal.IsSecurityTelemetryOnly || principal.ServerId is null
                || principal.DungeonSessionId != sessionId) return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (!long.TryParse(request.ConnectionGeneration, NumberStyles.None, CultureInfo.InvariantCulture, out long generation))
                return Results.BadRequest(new { error = "invalid_interaction_event" });
            try
            {
                var result = await coordinator.RecordAsync(sessionId, characterId, principal.ServerId,
                    new(request.EventId, request.TargetId, generation, request.ObservedAtUtc), ct);
                return Results.Ok(new { result.EventId, result.CharacterId,
                    Revision = result.Revision.ToString(CultureInfo.InvariantCulture), result.AppliedObjectives });
            }
            catch (NarrativeConflictException error) { return Failure(error); }
        });
        app.MapPost("/api/characters/{characterId:guid}/narrative/dungeon-objectives", async (
            Guid characterId, NarrativeDungeonObjectiveRequest request, HttpContext context,
            [Microsoft.AspNetCore.Mvc.FromServices] DungeonObjectiveCoordinator coordinator, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            // The player references a server-issued result; no client outcome or counts are trusted.
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (!long.TryParse(request.ExpectedRevision, NumberStyles.None, CultureInfo.InvariantCulture, out long revision))
                return Results.BadRequest(new { error = "invalid_narrative_revision" });
            try
            {
                return Results.Ok(ToResponse(await coordinator.RecordAsync(characterId, principal.SteamId,
                    new(request.RequestId, revision, request.CatalogVersion, request.DungeonSessionId, request.QuestId, request.ObjectiveId),
                    cancellationToken)));
            }
            catch (NarrativeConflictException error) { return Failure(error); }
        });
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
        "interaction_not_authorized" => Results.StatusCode(StatusCodes.Status403Forbidden),
        "kill_not_authorized" => Results.StatusCode(StatusCodes.Status403Forbidden),
        "invalid_kill_event" => Results.BadRequest(new { error = error.Message }),
        "invalid_interaction_event" => Results.BadRequest(new { error = error.Message }),
        "dungeon_clear_evidence_not_authorized" => Results.StatusCode(StatusCodes.Status403Forbidden),
        "invalid_narrative_command" => Results.BadRequest(new { error = error.Message }),
        _ => Results.Conflict(new { error = error.Message })
    };

    // Revisions are decimal strings across the UE/JSON boundary; never float-rounded.
    private static object ToResponse(NarrativeView view) => new
    {
        view.CatalogVersion, view.State.CharacterId, view.State.SchemaVersion,
        Revision = view.State.Revision.ToString(CultureInfo.InvariantCulture),
        Quests = view.Journal,
        view.State.CompletedChapters, view.State.StoryUnlocks, view.AvailableDialogues,
        DungeonObjectiveCandidates = view.DungeonObjectiveCandidates.IsDefault ? [] : view.DungeonObjectiveCandidates
    };
}
