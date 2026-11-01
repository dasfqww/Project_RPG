using System.Text.Json;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public sealed record PrepareDungeonPickupRequest(Guid DropId, ItemCommitRequestContract? Command);

public static class DungeonPickupIntentApiEndpoints
{
    private static bool Allowed(AuthenticatedPrincipal principal, Guid session) => principal.IsGameServer
        && !principal.IsSecurityTelemetryOnly && principal.DungeonSessionId == session
        && principal.ServerId is { } server && DungeonPickupIntentRules.ValidScope(session, server);

    private static IResult Reply(DungeonPickupIntentResult result) => result.Intent is { } intent
        ? Results.Ok(new { status = result.Status.ToString(), intent.DungeonSessionId, intent.ServerId, intent.DropId,
            intent.RequestId, intent.CharacterId, intent.ItemId, intent.PreparedAt, intent.ConsumedAt,
            command = JsonSerializer.Deserialize<ItemCommitRequestContract>(intent.CommandJson, DungeonPickupIntentRules.JsonOptions) })
        : result.Status switch
        {
            DungeonPickupIntentStatus.Forbidden => Results.StatusCode(403),
            DungeonPickupIntentStatus.NotFound => Results.NotFound(),
            DungeonPickupIntentStatus.Conflict => Results.Conflict(new { error = "pickup_intent_conflict" }),
            _ => Results.BadRequest(new { error = "invalid_pickup_intent" })
        };

    public static void MapDungeonPickupIntentApi(this WebApplication app)
    {
        app.MapPost("/api/dungeon-sessions/{sessionId:guid}/pickup-intents/{requestId:guid}/commit",
            async (Guid sessionId, Guid requestId, HttpContext context, DungeonPickupCommitService service,
                CancellationToken cancellation) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                var principal = context.GetProjectRpgPrincipal();
                if (!Allowed(principal, sessionId)) return Results.StatusCode(403);
                // No caller-supplied grant/actor/destination. Detect bodies even without Content-Length.
                if (context.Request.ContentLength > 0 || await context.Request.Body.ReadAsync(new byte[1], cancellation) != 0)
                    return Results.BadRequest(new { error = "pickup_commit_requires_empty_body" });
                var result = await service.CommitAsync(sessionId, principal.ServerId!, requestId, cancellation);
                if (result.Receipt is { } receipt) return Results.Ok(ItemApiContractMapper.ToContract(receipt));
                int statusCode = result.Status switch
                {
                    DungeonPickupCommitStatus.Forbidden => 403,
                    DungeonPickupCommitStatus.NotFound => 404,
                    DungeonPickupCommitStatus.InvalidRequest or DungeonPickupCommitStatus.ValidationFailed => 400,
                    DungeonPickupCommitStatus.IdempotencyConflict or DungeonPickupCommitStatus.RevisionConflict
                        or DungeonPickupCommitStatus.LocationConflict => 409,
                    _ => 500
                };
                return Results.Json(new { status = result.Status.ToString(), error = "pickup_commit_not_completed" }, statusCode: statusCode);
            });
        app.MapPost("/api/dungeon-sessions/{sessionId:guid}/pickup-intents",
            async (Guid sessionId, HttpContext context,
                IDungeonPickupIntentRepository repository, TimeProvider clock, CancellationToken cancellation) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                var principal = context.GetProjectRpgPrincipal();
                if (!Allowed(principal, sessionId)) return Results.StatusCode(403);
                if (!string.Equals(context.Request.ContentType?.Split(';')[0].Trim(), "application/json", StringComparison.OrdinalIgnoreCase))
                    return Results.StatusCode(415);
                const int limit = 128 * 1024;
                if (context.Request.ContentLength > limit) return Results.StatusCode(413);
                byte[] bytes = new byte[limit + 1];
                int count = 0;
                while (count <= limit)
                {
                    int read = await context.Request.Body.ReadAsync(bytes.AsMemory(count), cancellation);
                    if (read == 0) break;
                    count += read;
                }
                if (count > limit) return Results.StatusCode(413);
                PrepareDungeonPickupRequest? body;
                try { body = JsonSerializer.Deserialize<PrepareDungeonPickupRequest>(bytes.AsSpan(0, count), DungeonPickupIntentRules.JsonOptions); }
                catch (JsonException) { return Results.BadRequest(new { error = "invalid_pickup_intent" }); }
                if (body?.Command is null || !ItemApiContractMapper.TryToDomain(body.Command, out var command, out _))
                    return Results.BadRequest(new { error = "invalid_pickup_intent" });
                return Reply(await repository.PreparePickupAsync(sessionId, principal.ServerId!, body.DropId,
                    command, clock.GetUtcNow(), cancellation));
            });
        app.MapGet("/api/dungeon-sessions/{sessionId:guid}/pickup-intents/{requestId:guid}",
            async (Guid sessionId, Guid requestId, HttpContext context, IDungeonPickupIntentRepository repository,
                TimeProvider clock, CancellationToken cancellation) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                var principal = context.GetProjectRpgPrincipal();
                if (!Allowed(principal, sessionId)) return Results.StatusCode(403);
                return Reply(await repository.ReadPickupIntentAsync(sessionId, principal.ServerId!, requestId,
                    clock.GetUtcNow(), cancellation));
            });
    }
}
