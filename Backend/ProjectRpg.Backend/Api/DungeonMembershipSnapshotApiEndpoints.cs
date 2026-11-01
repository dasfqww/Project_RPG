using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using System.Globalization;

namespace ProjectRpg.Backend.Api;

public static class DungeonMembershipSnapshotApiEndpoints
{
    public static void MapDungeonMembershipSnapshotApi(this WebApplication app)
    {
        app.MapGet("/api/dungeon-sessions/{dungeonSessionId:guid}/membership-snapshot",
            async (Guid dungeonSessionId, HttpContext context, IGameRepository repository,
                CancellationToken cancellationToken) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
                if (!principal.IsGameServer || principal.DungeonSessionId != dungeonSessionId
                    || dungeonSessionId == Guid.Empty || string.IsNullOrWhiteSpace(principal.ServerId))
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                try
                {
                    DungeonMembershipSnapshot? snapshot = await repository.GetDungeonMembershipSnapshotAsync(
                        dungeonSessionId, principal.ServerId, cancellationToken);
                    if (snapshot is null) return Results.NotFound();
                    return Results.Ok(ToResponse(snapshot, DateTimeOffset.UtcNow));
                }
                catch (DungeonSessionServerMismatchException)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }
            });
    }

    // Presentation metadata is additive and remains behind the existing exact
    // game-server/session authorization. This mapper never expires membership.
    public static DungeonMembershipSnapshotResponse ToResponse(DungeonMembershipSnapshot snapshot, DateTimeOffset observedAt)
        => new(snapshot.DungeonSessionId,
            snapshot.ServerId, snapshot.State.ToString(), snapshot.UpdatedAt, snapshot.ExpiresAt,
            snapshot.Members.Select(member => new DungeonMembershipSnapshotMemberResponse(
                member.CharacterId, member.SteamId, member.LeaseExpiresAt,
                member.CharacterName is { Length: <= 20 } name ? name : null,
                member.DisconnectedAt, ReconnectDeadline(member.DisconnectedAt),
                member.ConnectionGeneration.ToString(CultureInfo.InvariantCulture),
                member.DisconnectedAt is not null
                    ? member.DisconnectedGeneration.ToString(CultureInfo.InvariantCulture) : null)).ToArray(),
            snapshot.Abandonments.Select(receipt => new DungeonMembershipSnapshotAbandonmentResponse(
                receipt.CharacterId, receipt.SteamId, receipt.AbandonedAt,
                receipt.SessionStateAtAbandonment.ToString(), true)).ToArray(),
            snapshot.DisconnectionExpulsions.Select(receipt => new DungeonDisconnectionExpulsionResponse(
                receipt.CharacterId, receipt.SteamId, receipt.DisconnectedAt,
                receipt.ExpiredAt, receipt.ProcessedAt, true)).ToArray(), snapshot.ObservedAt ?? observedAt);

    private static DateTimeOffset? ReconnectDeadline(DateTimeOffset? disconnectedAt)
    {
        if (disconnectedAt is not DateTimeOffset first) return null;
        // Optional display metadata must not break the membership/receipt read
        // when a persisted timestamp cannot represent its grace deadline.
        try { return first.ToUniversalTime() + DungeonConnectionPolicy.UnexpectedDisconnectGrace; }
        catch (ArgumentOutOfRangeException) { return null; }
    }
}
