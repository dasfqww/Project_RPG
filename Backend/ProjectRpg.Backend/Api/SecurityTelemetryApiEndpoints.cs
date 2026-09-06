using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class SecurityTelemetryApiEndpoints
{
    public static IEndpointRouteBuilder MapSecurityTelemetryApi(
        this IEndpointRouteBuilder endpoints)
    {
        endpoints.MapPost(
            "/api/security/events/batch",
            async (
                SecurityEventBatchRequest request,
                HttpContext context,
                IGameRepository gameRepository,
                ISecurityTelemetryRepository telemetryRepository,
                IOptions<SecurityTelemetryOptions> telemetryOptions,
                TimeProvider timeProvider,
                CancellationToken cancellationToken) =>
            {
                AuthenticatedPrincipal principal =
                    context.GetProjectRpgPrincipal();
                if (!principal.IsGameServer
                    || string.IsNullOrWhiteSpace(principal.ServerId)
                    || !principal.MatchesGameServer(
                        principal.ServerId,
                        request.DungeonSessionId))
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }

                if (request.DungeonSessionId == Guid.Empty
                    || request.Events is null
                    || request.Events.Count is < 1
                        or > SecurityTelemetryRules.MaximumBatchSize)
                {
                    return Results.BadRequest(new
                    {
                        error = $"A batch requires 1 to {SecurityTelemetryRules.MaximumBatchSize} events and a non-empty DungeonSessionId."
                    });
                }

                DateTimeOffset receivedAt = timeProvider.GetUtcNow();
                List<SecurityTelemetryEvent> events = [];
                HashSet<Guid> eventIds = [];
                foreach (SecurityEventContract? contract in request.Events)
                {
                    if (contract is null)
                    {
                        return Results.BadRequest(new
                        {
                            error = "A batch cannot contain a null event."
                        });
                    }

                    if (!TryToDomain(
                        contract,
                        request.DungeonSessionId,
                        principal.ServerId,
                        receivedAt,
                        out SecurityTelemetryEvent securityEvent,
                        out string error))
                    {
                        return Results.BadRequest(new { error });
                    }

                    if (!eventIds.Add(securityEvent.EventId))
                    {
                        return Results.BadRequest(new
                        {
                            error = "A batch cannot contain duplicate EventId values."
                        });
                    }

                    events.Add(securityEvent);
                }

                SecurityTelemetryMember[] requestedMembers = events
                    .Select(value => new SecurityTelemetryMember(
                        value.CharacterId,
                        value.SteamId))
                    .Distinct()
                    .ToArray();
                int graceSeconds = Math.Clamp(
                    telemetryOptions.Value.PostSessionGraceSeconds,
                    0,
                    60 * 60);
                bool allMembersAuthorized = await gameRepository
                    .AreAuthorizedSecurityTelemetryMembersAsync(
                        request.DungeonSessionId,
                        principal.ServerId,
                        requestedMembers,
                        receivedAt,
                        TimeSpan.FromSeconds(graceSeconds),
                        cancellationToken);
                if (!allMembersAuthorized)
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                SecurityEventStoreResult result =
                    await telemetryRepository.StoreBatchAsync(
                        events,
                        cancellationToken);
                if (result.ConflictingEventIds.Count > 0)
                {
                    return Results.Conflict(new
                    {
                        error = "An EventId was previously stored with different immutable content.",
                        eventIds = result.ConflictingEventIds
                    });
                }

                return Results.Ok(new SecurityEventBatchResponse(
                    result.AcceptedCount,
                    result.DuplicateCount));
            })
            .WithMetadata(new RequestSizeLimitAttribute(128 * 1024));

        endpoints.MapGet(
            "/api/security/sessions/{dungeonSessionId:guid}/summary",
            async (
                Guid dungeonSessionId,
                HttpContext context,
                ISecurityTelemetryRepository telemetryRepository,
                CancellationToken cancellationToken) =>
            {
                AuthenticatedPrincipal principal =
                    context.GetProjectRpgPrincipal();
                bool canRead = principal.IsAdministrator
                    || (principal.IsGameServer
                        && !string.IsNullOrWhiteSpace(principal.ServerId)
                        && principal.MatchesGameServer(
                            principal.ServerId,
                            dungeonSessionId));
                if (!canRead)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }

                SecuritySessionSummary summary =
                    await telemetryRepository.GetSessionSummaryAsync(
                        dungeonSessionId,
                        cancellationToken);
                return Results.Ok(ToContract(summary));
            });

        return endpoints;
    }

    private static bool TryToDomain(
        SecurityEventContract contract,
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset receivedAt,
        out SecurityTelemetryEvent securityEvent,
        out string error)
    {
        string steamId = contract.SteamId?.Trim() ?? string.Empty;
        string typeText = contract.Type?.Trim() ?? string.Empty;
        string severityText = contract.Severity?.Trim() ?? string.Empty;
        string detail = contract.Detail ?? string.Empty;
        if (!Enum.TryParse(
                typeText,
                ignoreCase: false,
                out SecurityEventType type)
            || !Enum.IsDefined(type))
        {
            securityEvent = default!;
            error = "Type is not a supported server security event type.";
            return false;
        }

        if (!Enum.TryParse(
                severityText,
                ignoreCase: false,
                out SecurityEventSeverity severity)
            || !Enum.IsDefined(severity))
        {
            securityEvent = default!;
            error = "Severity must be Low, Medium, High, or Critical.";
            return false;
        }

        string fingerprint = CreateFingerprint(
            contract,
            dungeonSessionId,
            steamId,
            serverId,
            type,
            severity,
            detail);
        securityEvent = new SecurityTelemetryEvent(
            contract.EventId,
            dungeonSessionId,
            contract.CharacterId,
            steamId,
            serverId,
            type,
            severity,
            contract.Score,
            contract.RiskAfter,
            contract.ServerTimeSeconds,
            detail,
            fingerprint,
            receivedAt);
        return SecurityTelemetryRules.TryValidate(securityEvent, out error);
    }

    private static string CreateFingerprint(
        SecurityEventContract contract,
        Guid dungeonSessionId,
        string steamId,
        string serverId,
        SecurityEventType type,
        SecurityEventSeverity severity,
        string detail)
    {
        string canonical = string.Join(
            '\u001f',
            contract.EventId.ToString("D"),
            dungeonSessionId.ToString("D"),
            contract.CharacterId.ToString("D"),
            steamId,
            serverId,
            type.ToString(),
            severity.ToString(),
            contract.Score.ToString("R", CultureInfo.InvariantCulture),
            contract.RiskAfter.ToString("R", CultureInfo.InvariantCulture),
            contract.ServerTimeSeconds.ToString("R", CultureInfo.InvariantCulture),
            detail);
        return Convert.ToHexString(
            SHA256.HashData(Encoding.UTF8.GetBytes(canonical)));
    }

    private static SecuritySessionSummaryResponse ToContract(
        SecuritySessionSummary summary)
    {
        return new SecuritySessionSummaryResponse(
            summary.DungeonSessionId,
            summary.TotalEvents,
            summary.TotalScore,
            summary.MaximumRiskAfter,
            summary.FirstReceivedAt,
            summary.LastReceivedAt,
            summary.ByType.Select(value =>
                new SecurityTelemetryBreakdownContract(
                    value.Key,
                    value.EventCount,
                    value.TotalScore)).ToArray(),
            summary.BySeverity.Select(value =>
                new SecurityTelemetryBreakdownContract(
                    value.Key,
                    value.EventCount,
                    value.TotalScore)).ToArray(),
            summary.ByCharacter.Select(value =>
                new SecurityCharacterBreakdownContract(
                    value.CharacterId,
                    value.EventCount,
                    value.TotalScore,
                    value.MaximumRiskAfter)).ToArray());
    }
}
