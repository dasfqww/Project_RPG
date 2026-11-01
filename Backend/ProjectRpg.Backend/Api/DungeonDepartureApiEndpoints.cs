using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class DungeonDepartureApiEndpoints
{
    public static void MapDungeonDepartureApi(this WebApplication app)
    {
        app.MapPost("/api/dungeon-sessions/{dungeonSessionId:guid}/departure", Request);
        app.MapPost("/api/dungeon-sessions/{dungeonSessionId:guid}/departure/cancel", Cancel);
    }

    private static Task<IResult> Request(Guid dungeonSessionId, DungeonDepartureRequest request, HttpContext context,
        IGameRepository repository, DungeonContentCatalog catalog, IOptions<DungeonSessionOptions> options,
        TimeProvider clock, CancellationToken cancellationToken) =>
        Apply(dungeonSessionId, request, context, repository, catalog, options.Value, clock.GetUtcNow(), false, cancellationToken);
    private static Task<IResult> Cancel(Guid dungeonSessionId, DungeonDepartureRequest request, HttpContext context,
        IGameRepository repository, DungeonContentCatalog catalog, IOptions<DungeonSessionOptions> options,
        TimeProvider clock, CancellationToken cancellationToken) =>
        Apply(dungeonSessionId, request, context, repository, catalog, options.Value, clock.GetUtcNow(), true, cancellationToken);

    private static async Task<IResult> Apply(Guid sessionId, DungeonDepartureRequest request, HttpContext context,
        IGameRepository repository, DungeonContentCatalog catalog, DungeonSessionOptions options, DateTimeOffset now,
        bool cancel, CancellationToken cancellationToken)
    {
        context.Response.Headers.CacheControl = "no-store";
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        if (sessionId == Guid.Empty || request.CharacterId == Guid.Empty || request.RequestId == Guid.Empty
            || request.ExpectedLobbyRevision is < 1 or > DungeonDeparturePolicy.MaximumRevision)
            return Results.BadRequest(new { error = "invalid_departure_command" });
        DungeonSession? session = await repository.GetDungeonSessionAsync(sessionId, now, cancellationToken);
        if (session is null) return Results.NotFound();
        if (session.LeaderCharacterId != request.CharacterId || !session.Members.Any(member =>
            member.CharacterId == request.CharacterId && member.SteamId == principal.SteamId))
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        // Cancel/recovery remain possible if operators removed the content.
        // An exact already committed request is recovery, not a new content
        // selection. Never return this snapshot directly: the repository must
        // revalidate the receipt/member/lease atomically if cancellation or the
        // roster changed after this read.
        if (!cancel && catalog.GetSelectionError(session.DungeonId, session.Difficulty) is not null
            && !DungeonDeparturePolicy.IsCommittedRequestReplay(session, request.RequestId, request.ExpectedLobbyRevision))
            return Results.Conflict(new { error = "dungeon_content_not_available" });
        try
        {
            DungeonSession updated = await repository.SetDungeonDepartureAsync(sessionId, principal.SteamId,
                request.CharacterId, request.RequestId, request.ExpectedLobbyRevision, cancel,
                Math.Clamp(options.MinimumDeparturePartySize, 1, Math.Clamp(options.MaxPartySize, 1, 8)), now, cancellationToken);
            return Results.Ok(DungeonSessionResponses.From(updated));
        }
        catch (DungeonSessionNotFoundException) { return Results.NotFound(); }
        catch (DungeonSessionMembershipException) { return Results.StatusCode(StatusCodes.Status403Forbidden); }
        catch (DungeonDepartureConflictException exception) { return Results.Conflict(new { error = exception.Message }); }
        catch (DungeonSessionStateConflictException) { return Results.Conflict(new { error = "dungeon_departure_not_available" }); }
    }
}
