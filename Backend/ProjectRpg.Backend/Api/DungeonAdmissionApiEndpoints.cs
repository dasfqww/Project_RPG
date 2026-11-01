using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class DungeonAdmissionApiEndpoints
{
    public static void MapDungeonAdmissionApi(this WebApplication app)
    {
        app.MapPost("/api/dungeon-sessions/{dungeonSessionId:guid}/disconnection-expirations",
            async (Guid dungeonSessionId, HttpContext context, IGameRepository repository,
                TimeProvider timeProvider, CancellationToken cancellationToken) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
                if (!principal.IsGameServer || principal.DungeonSessionId != dungeonSessionId
                    || dungeonSessionId == Guid.Empty || string.IsNullOrWhiteSpace(principal.ServerId))
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                try
                {
                    IReadOnlyList<DungeonDisconnectionExpulsion> receipts =
                        await repository.ExpelDisconnectedDungeonMembersAsync(
                            dungeonSessionId, principal.ServerId, timeProvider.GetUtcNow(), cancellationToken);
                    return Results.Ok(new
                    {
                        disconnectionExpulsions = receipts.Select(receipt => new DungeonDisconnectionExpulsionResponse(
                            receipt.CharacterId, receipt.SteamId, receipt.DisconnectedAt,
                            receipt.ExpiredAt, receipt.ProcessedAt, true)).ToArray()
                    });
                }
                catch (DungeonSessionNotFoundException)
                {
                    return Results.NotFound(new { error = "dungeon_session_not_found" });
                }
                catch (DungeonSessionServerMismatchException)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }
            });

        // Only the assigned game server may report a socket it observed losing.
        // The client cannot choose a timestamp or reset the grace period.
        app.MapPost("/api/dungeon-sessions/{dungeonSessionId:guid}/members/{characterId:guid}/disconnection",
            async (Guid dungeonSessionId, Guid characterId, RecordDungeonDisconnectionRequest request,
                HttpContext context, IGameRepository repository, TimeProvider timeProvider,
                CancellationToken cancellationToken) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
                if (!principal.IsGameServer || principal.DungeonSessionId != dungeonSessionId
                    || dungeonSessionId == Guid.Empty || characterId == Guid.Empty
                    || string.IsNullOrWhiteSpace(principal.ServerId))
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                if (request.SteamId is null || request.SteamId.Length is < 1 or > 20
                    || !request.SteamId.All(char.IsAsciiDigit) || request.ConnectionGeneration <= 0)
                    return Results.BadRequest(new { error = "invalid_disconnection_report" });

                try
                {
                    DateTimeOffset disconnectedAt = await repository.RecordDungeonMemberDisconnectionAsync(
                        dungeonSessionId, principal.ServerId, request.SteamId, characterId,
                        request.ConnectionGeneration, timeProvider.GetUtcNow(), cancellationToken,
                        request.ObservedAtUtc);
                    return Results.Ok(new { disconnectedAt });
                }
                catch (ArgumentException)
                {
                    return Results.BadRequest(new { error = "invalid_disconnection_observation" });
                }
                catch (DungeonSessionNotFoundException)
                {
                    return Results.NotFound(new { error = "dungeon_session_not_found" });
                }
                catch (Exception exception) when (exception is
                    DungeonSessionServerMismatchException or DungeonSessionMembershipException)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }
                catch (DungeonSessionStateConflictException)
                {
                    return Results.Conflict(new { error = "dungeon_disconnection_not_available" });
                }
            });

        app.MapPost("/api/dungeon-sessions/{dungeonSessionId:guid}/members/{characterId:guid}/admission-confirmation",
            async (Guid dungeonSessionId, Guid characterId, ConfirmDungeonAdmissionRequest request,
                HttpContext context, IGameRepository repository, TimeProvider timeProvider,
                CancellationToken cancellationToken) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
                if (!principal.IsGameServer || principal.DungeonSessionId != dungeonSessionId
                    || dungeonSessionId == Guid.Empty || characterId == Guid.Empty
                    || string.IsNullOrWhiteSpace(principal.ServerId))
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                if (request.SteamId is null || request.SteamId.Length is < 1 or > 20
                    || !request.SteamId.All(char.IsAsciiDigit) || request.ConnectionGeneration <= 0)
                    return Results.BadRequest(new { error = "invalid_admission_confirmation" });

                try
                {
                    bool confirmed = await repository.ConfirmDungeonMemberAdmissionAsync(
                        dungeonSessionId, principal.ServerId, request.SteamId, characterId,
                        request.ConnectionGeneration, timeProvider.GetUtcNow(), cancellationToken);
                    return confirmed
                        ? Results.Ok(new { admissionConfirmed = true })
                        : Results.Conflict(new { error = "dungeon_admission_not_available" });
                }
                catch (DungeonSessionNotFoundException)
                {
                    return Results.NotFound(new { error = "dungeon_session_not_found" });
                }
                catch (Exception exception) when (exception is
                    DungeonSessionServerMismatchException or DungeonSessionMembershipException)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }
                catch (DungeonSessionStateConflictException)
                {
                    return Results.Conflict(new { error = "dungeon_admission_not_available" });
                }
            });
    }
}
