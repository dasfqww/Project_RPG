using System.Globalization;
using Npgsql;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Api;

public static partial class RidingApiEndpoints
{
    private static void MapRidingServerSnapshotApi(WebApplication app)
    {
        app.MapGet("/api/game-servers/sessions/{sessionId:guid}/members/{characterId:guid}/riding", async (
            Guid sessionId, Guid characterId, HttpContext c, IRidingAdmissionSnapshotStore store, RidingRuntime runtime, RidingRequestLimiter limiter, CancellationToken ct) =>
        {
            c.Response.Headers.CacheControl = "no-store";
            var principal = c.GetProjectRpgPrincipal();
            if (!principal.IsGameServer || principal.IsSecurityTelemetryOnly || principal.DungeonSessionId != sessionId || string.IsNullOrWhiteSpace(principal.ServerId))
                return Results.StatusCode(403);
            if (!limiter.Acquire("server:" + principal.ServerId, false))
            { c.Response.Headers.RetryAfter = "1"; return Results.Json(new { error = "RateLimited" }, statusCode: 429); }
            var q = c.Request.Query;
            if (q.Count != 3 || !q.TryGetValue("connectionGeneration", out var generationText) || generationText.Count != 1
                || !q.TryGetValue("catalogVersion", out var versionText) || versionText.Count != 1 || !q.TryGetValue("gameplayHash", out var hash) || hash.Count != 1
                || !long.TryParse(generationText[0], NumberStyles.None, CultureInfo.InvariantCulture, out long generation) || generation <= 0 || generationText[0] != RidingApiContracts.Number(generation)
                || !int.TryParse(versionText[0], NumberStyles.None, CultureInfo.InvariantCulture, out int version) || version <= 0 || versionText[0] != version.ToString(CultureInfo.InvariantCulture)
                || hash[0] is not { Length: 64 } gameplayHash || gameplayHash.Any(ch => ch is not (>= '0' and <= '9' or >= 'a' and <= 'f')))
                return Results.BadRequest(new { error = "InvalidRequest" });
            if (runtime.Catalog is null) return Results.NotFound(new { error = "RidingDisabled" });
            try
            {
                runtime.Catalog.RequireCompatible(version, gameplayHash);
                var snapshot = await store.GetRidingAdmissionSnapshotAsync(principal.ServerId, sessionId, characterId, generation, ct);
                if (snapshot is null) return Results.NotFound(new { error = "NotFound" });
                return Results.Ok(new { protocolVersion = 1, serverId = principal.ServerId, snapshot.DungeonSessionId, snapshot.CharacterId, snapshot.RosterId,
                    connectionGeneration = RidingApiContracts.Number(snapshot.ConnectionGeneration), snapshot.ObservedAtUtc, snapshot.ValidUntilUtc,
                    catalogVersion = runtime.Catalog.CatalogVersion, gameplayHash = runtime.Catalog.GameplayHash,
                    rosterRevision = RidingApiContracts.Number(snapshot.Owner.Collection.Revision), mounts = snapshot.Owner.Collection.Mounts,
                    preferencesRevision = RidingApiContracts.Number(snapshot.Owner.Preferences.Revision), preferences = PreferencesContract(snapshot.Owner.Preferences) });
            }
            catch (RidingRuleException e) { return Results.BadRequest(new { error = e.Code.ToString() }); }
            catch (Exception e) when (e is NpgsqlException or TimeoutException)
            { c.Response.Headers.RetryAfter = "1"; return Results.Json(new { error = "StorageUnavailable" }, statusCode: 503); }
        });
    }
}
