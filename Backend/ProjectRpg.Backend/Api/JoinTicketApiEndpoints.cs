using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Validation;

namespace ProjectRpg.Backend.Api;

public static class JoinTicketApiEndpoints
{
    public static IEndpointRouteBuilder MapJoinTicketApi(this IEndpointRouteBuilder endpoints)
    {
        endpoints.MapPost("/api/join-tickets", async (
            CreateJoinTicketRequest request, HttpContext context, IGameRepository repository,
            AccessTokenService accessTokenService, IOptions<AuthOptions> authOptions,
            CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);

            int lifetimeSeconds = Math.Clamp(authOptions.Value.JoinTicketLifetimeSeconds, 10, 5 * 60);
            IssuedAccessToken ticket = accessTokenService.Issue(TimeSpan.FromSeconds(lifetimeSeconds));
            JoinTicketIssueResult result = await repository.StoreJoinTicketAsync(ticket.TokenHash,
                principal.SteamId, request.CharacterId, request.DungeonSessionId, ticket.ExpiresAt,
                cancellationToken);
            if (result != JoinTicketIssueResult.Issued)
                return Results.Conflict(new
                {
                    error = result == JoinTicketIssueResult.ReconnectGraceExpired
                        ? "dungeon_reconnect_grace_expired" : "dungeon_member_not_active"
                });

            return Results.Ok(new CreateJoinTicketResponse(request.DungeonSessionId,
                request.CharacterId, ticket.Token, ticket.ExpiresAt));
        });
        endpoints.MapPost("/api/join-tickets/consume", async (
            ConsumeJoinTicketRequest request, HttpContext context, IGameRepository repository,
            AccessTokenService accessTokenService, TimeProvider timeProvider,
            CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (!principal.IsGameServer)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (string.IsNullOrWhiteSpace(request.JoinTicket)
                || RequestValidators.ValidateServerId(request.ServerId, out string serverId) is not null)
                return Results.Unauthorized();
            if (!principal.DungeonSessionId.HasValue
                || !principal.MatchesGameServer(serverId, principal.DungeonSessionId.Value))
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (request.ConsumptionRequestId == Guid.Empty)
                return Results.BadRequest(new { error = "invalid_consumption_request_id" });
            string ticket = request.JoinTicket.Trim();
            if (ticket.Length is < 32 or > 256) return Results.Unauthorized();
            ConsumedJoinTicket? consumed = await repository.ConsumeJoinTicketAsync(
                accessTokenService.Hash(ticket), serverId, principal.DungeonSessionId.Value,
                timeProvider.GetUtcNow(), cancellationToken, request.ConsumptionRequestId);
            return consumed is null
                ? Results.Json(new { error = "join_ticket_not_recoverable" }, statusCode: StatusCodes.Status401Unauthorized)
                : Results.Ok(new ConsumeJoinTicketResponse(consumed.DungeonSessionId,
                    consumed.CharacterId, consumed.SteamId, consumed.ConnectionGeneration));
        });
        endpoints.MapPost("/api/join-tickets/consumption-evidence", async (
            ReadJoinTicketConsumptionRequest request, HttpContext context, IGameRepository repository,
            TimeProvider timeProvider, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (!principal.IsGameServer) return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (RequestValidators.ValidateServerId(request.ServerId, out string serverId) is not null
                || request.DungeonSessionId == Guid.Empty || request.ConsumptionRequestId == Guid.Empty
                || request.TicketHash is null || request.TicketHash.Length != 64
                || request.TicketHash.Any(c => !char.IsAsciiHexDigit(c))
                || request.ExpectedSteamId is null || request.ExpectedSteamId.Length is < 1 or > 20
                || request.ExpectedSteamId.Any(c => c is < '0' or > '9'))
                return Results.BadRequest(new { error = "invalid_consumption_evidence_request" });
            if (!principal.MatchesGameServer(serverId, request.DungeonSessionId))
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            var evidence = await repository.ReadJoinTicketConsumptionEvidenceAsync(
                request.TicketHash.ToUpperInvariant(), serverId, request.DungeonSessionId,
                request.ConsumptionRequestId, request.ExpectedSteamId, timeProvider.GetUtcNow(), cancellationToken);
            // Missing evidence may have been removed by an older deployment.
            // It cannot authorize clearing an unresolved-consumption barrier.
            return evidence is null ? Results.Conflict(new { error = "join_ticket_consumption_unresolved" })
                : Results.Ok(new ReadJoinTicketConsumptionResponse("Recorded", evidence));
        });
        endpoints.MapPost("/api/join-tickets/seal-consumption", async (
            SealJoinTicketConsumptionRequest request, HttpContext context, IGameRepository repository,
            TimeProvider timeProvider, CancellationToken cancellationToken) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
            if (!principal.IsGameServer) return Results.StatusCode(StatusCodes.Status403Forbidden);
            if (RequestValidators.ValidateServerId(request.ServerId, out string serverId) is not null
                || request.DungeonSessionId == Guid.Empty || request.ConsumptionRequestId == Guid.Empty
                || request.TicketHash is null || request.TicketHash.Length != 64
                || request.TicketHash.Any(c => !char.IsAsciiHexDigit(c))
                || request.ExpectedSteamId is null || request.ExpectedSteamId.Length is < 1 or > 20
                || request.ExpectedSteamId.Any(c => c is < '0' or > '9'))
                return Results.BadRequest(new { error = "invalid_consumption_seal_request" });
            if (!principal.MatchesGameServer(serverId, request.DungeonSessionId))
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            var resolution = await repository.SealJoinTicketConsumptionAsync(
                request.TicketHash.ToUpperInvariant(), serverId, request.DungeonSessionId,
                request.ConsumptionRequestId, request.ExpectedSteamId, timeProvider.GetUtcNow(), cancellationToken);
            return resolution is null ? Results.Conflict(new { error = "join_ticket_consumption_unresolved" })
                : Results.Ok(resolution);
        });
        return endpoints;
    }
}
