using System.Text.Json;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Api;

public static class TradeApiEndpoints
{
    public const int MaximumBodyBytes = 16 * 1024;
    private const string Root = "/api/characters/{characterId:guid}/trades";

    public static void MapTradeApi(this WebApplication app)
    {
        app.MapGet(Root + "/{tradeId:guid}/assets", async (Guid characterId, Guid tradeId, HttpContext context,
            TradeApplication service, ITradeSessionRepository store, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            try
            {
                var actor = await service.ActorAsync(context.GetProjectRpgPrincipal(), characterId, ct);
                var assets = await store.AssetsAsync(tradeId, actor, ct);
                return assets is null ? Failure(context, "trade_not_found") : Results.Ok(TradeApiContracts.Assets(assets));
            }
            catch (TradeRuleException e) { return Failure(context, e.Code); }
        });
        app.MapGet(Root + "/targets", async (Guid characterId, HttpContext context, TradeApplication service, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            try
            {
                var actor = await service.ActorAsync(context.GetProjectRpgPrincipal(), characterId, ct);
                if (context.Request.Query.Count != 1 || context.Request.Query["name"].Count != 1)
                    throw new TradeRuleException("invalid_trade_target_query");
                var targets = await service.FindTargetsAsync(actor, context.Request.Query["name"][0], ct);
                return Results.Ok(new { targets });
            }
            catch (TradeRuleException e) { return Failure(context, e.Code); }
        });
        app.MapGet(Root + "/{tradeId:guid}/preview", async (Guid characterId, Guid tradeId, HttpContext context,
            TradeApplication service, ITradeSessionRepository store, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            try
            {
                var actor = await service.ActorAsync(context.GetProjectRpgPrincipal(), characterId, ct);
                var preview = await store.PreviewAsync(tradeId, actor, ct);
                return preview is null ? Failure(context, "trade_not_found") : Results.Ok(TradeApiContracts.Preview(preview));
            }
            catch (TradeRuleException e) { return Failure(context, e.Code); }
        });
        app.MapGet(Root, (Guid characterId, HttpContext context, TradeApplication service, CancellationToken ct) =>
            Read(characterId, null, context, service, ct));
        app.MapGet(Root + "/{tradeId:guid}", (Guid characterId, Guid tradeId, HttpContext context, TradeApplication service, CancellationToken ct) =>
            Read(characterId, tradeId, context, service, ct));
        foreach (var (action, kind) in new[] { ("invite", TradeCommandKind.Invite), ("accept", TradeCommandKind.Accept),
            ("offer", TradeCommandKind.ReplaceOffer), ("ready", TradeCommandKind.Ready), ("withdraw", TradeCommandKind.Withdraw),
            ("confirm", TradeCommandKind.Confirm), ("cancel", TradeCommandKind.Cancel) })
        {
            app.MapPost(Root + "/{tradeId:guid}/" + action, async (Guid characterId, Guid tradeId,
                HttpContext context, TradeApplication service, CancellationToken ct) =>
            {
                context.Response.Headers.CacheControl = "no-store";
                try
                {
                    var actor = await service.ActorAsync(context.GetProjectRpgPrincipal(), characterId, ct);
                    if (tradeId == Guid.Empty) throw new TradeRuleException("invalid_trade_command");
                    var (request, failure) = await Body(context, ct);
                    if (failure is not null) return failure;
                    var parsed = TradeApiContracts.Parse(request!, kind);
                    var result = await service.ExecuteAsync(actor, tradeId, request!.RequestId, kind, parsed.Revision,
                        parsed.OfferRevision, request.InviteeCharacterId, parsed.Offer, ct);
                    // A persisted refusal includes the updated state/receipt; callers must keep that state even on 409.
                    return Results.Json(TradeApiContracts.Result(result), statusCode: result.Rejection is null ? 200 : 409);
                }
                catch (TradeRuleException e) { return Failure(context, e.Code); }
            });
        }
    }

    private static async Task<IResult> Read(Guid characterId, Guid? tradeId, HttpContext context, TradeApplication service, CancellationToken ct)
    {
        context.Response.Headers.CacheControl = "no-store";
        try
        {
            var actor = await service.ActorAsync(context.GetProjectRpgPrincipal(), characterId, ct);
            var session = await service.FindAsync(tradeId, actor, ct);
            if (tradeId.HasValue && session is null) return Failure(context, "trade_not_found");
            return Results.Ok(new { session = session is null ? null : TradeApiContracts.Session(session) });
        }
        catch (TradeRuleException e) { return Failure(context, e.Code); }
    }

    private static IResult Failure(HttpContext context, string code)
    {
        int status = code switch
        {
            "trade_player_required" or "character_not_owned" => 403,
            "trade_not_found" or "trade_participant_required" or "trade_target_unavailable" or "trading_disabled" => 404,
            "invalid_trade_command" or "invalid_trade_invitation" or "invalid_offer" or "invalid_trade_target_query" => 400,
            "trade_rate_limit" => 429,
            _ => 409
        };
        if (status == 429) context.Response.Headers.RetryAfter = "60";
        // Do not distinguish an existing foreign trade from an absent one.
        return Results.Json(new { error = code == "trade_participant_required" ? "trade_not_found" : code }, statusCode: status);
    }

    private static async Task<(TradeCommandRequest? Request, IResult? Failure)> Body(HttpContext context, CancellationToken ct)
    {
        if (!context.Request.HasJsonContentType()) return (null, Results.StatusCode(415));
        if (context.Request.ContentLength > MaximumBodyBytes) return (null, Results.StatusCode(413));
        using MemoryStream bytes = new();
        byte[] buffer = new byte[4096];
        while (true)
        {
            int remaining = MaximumBodyBytes - (int)bytes.Length;
            int count = await context.Request.Body.ReadAsync(buffer.AsMemory(0, Math.Min(buffer.Length, remaining + 1)), ct);
            if (count == 0) break;
            if (count > remaining) return (null, Results.StatusCode(413));
            bytes.Write(buffer, 0, count);
        }
        try
        {
            using var json = JsonDocument.Parse(bytes.ToArray(), new JsonDocumentOptions { MaxDepth = 12 });
            TradeApiContracts.RejectDuplicateProperties(json.RootElement);
            var request = json.RootElement.Deserialize<TradeCommandRequest>(TradeApiContracts.InputOptions);
            return request is null ? (null, Failure(context, "invalid_trade_command")) : (request, null);
        }
        catch (JsonException) { return (null, Failure(context, "invalid_trade_command")); }
    }
}
