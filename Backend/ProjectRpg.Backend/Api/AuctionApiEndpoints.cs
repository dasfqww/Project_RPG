using System.Text.Json;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Api;

public static class AuctionApiEndpoints
{
    public const int MaximumBodyBytes = 8192;
    private const string Root = "/api/characters/{characterId:guid}/auctions";
    public static void MapAuctionApi(this WebApplication app)
    {
        app.MapGet(Root, (Guid characterId, HttpContext c, AuctionApplication a, CancellationToken ct) =>
            Run(c, async () => { var actor = await a.ActorAsync(c.GetProjectRpgPrincipal(), characterId, true, ct);
                return AuctionApiContracts.Page(await a.SearchAsync(AuctionApiContracts.Query(c.Request.Query), actor, false, ct), false); }));
        app.MapGet(Root + "/mine", (Guid characterId, HttpContext c, AuctionApplication a, CancellationToken ct) =>
            Run(c, async () => { var actor = await a.ActorAsync(c.GetProjectRpgPrincipal(), characterId, false, ct);
                return AuctionApiContracts.Page(await a.SearchAsync(AuctionApiContracts.Query(c.Request.Query), actor, true, ct), true); }));
        app.MapGet(Root + "/requests/{requestId:guid}", (Guid characterId, Guid requestId, HttpContext c, AuctionApplication a, CancellationToken ct) =>
            Run(c, async () => { NoQuery(c); var actor = await a.ActorAsync(c.GetProjectRpgPrincipal(), characterId, false, ct);
                var r = await a.ReceiptAsync(requestId, actor, ct) ?? throw new TradeRuleException("auction_not_found"); return AuctionApiContracts.Receipt(r); }));
        app.MapGet(Root + "/requests/{requestId:guid}/assets", (Guid characterId, Guid requestId, HttpContext c, AuctionApplication a, CancellationToken ct) =>
            Run(c, async () => { NoQuery(c); var actor = await a.ActorAsync(c.GetProjectRpgPrincipal(), characterId, false, ct);
                var r = await a.AssetsAsync(requestId, actor, ct) ?? throw new TradeRuleException("auction_not_found");
                return AuctionApiContracts.Assets(r); }));
        app.MapGet(Root + "/{listingId:guid}", (Guid characterId, Guid listingId, HttpContext c, AuctionApplication a, CancellationToken ct) =>
            Run(c, async () => { NoQuery(c); var actor = await a.ActorAsync(c.GetProjectRpgPrincipal(), characterId, false, ct);
                var l = await a.FindAsync(listingId, actor, ct) ?? throw new TradeRuleException("auction_not_found"); return AuctionApiContracts.Listing(l, l.State.Seller.AccountId == actor.AccountId); }));
        app.MapGet(Root + "/{listingId:guid}/payable", (Guid characterId, Guid listingId, HttpContext c, AuctionApplication a, CancellationToken ct) =>
            Run(c, async () => { NoQuery(c); var actor = await a.ActorAsync(c.GetProjectRpgPrincipal(), characterId, false, ct);
                var p = await a.PayableAsync(listingId, actor, ct) ?? throw new TradeRuleException("auction_not_found"); return AuctionApiContracts.Payable(p); }));
        foreach (string action in new[] { "register", "purchase", "cancel", "claim" })
        {
            app.MapPost(Root + "/{listingId:guid}/" + action, async (Guid characterId, Guid listingId, HttpContext c, AuctionApplication a, CancellationToken ct) =>
            {
                c.Response.Headers.CacheControl = "no-store";
                try
                {
                    NoQuery(c); var actor = await a.ActorAsync(c.GetProjectRpgPrincipal(), characterId, false, ct);
                    if (listingId == Guid.Empty) throw AuctionApiContracts.Invalid();
                    if (!c.Request.HasJsonContentType()) return Results.StatusCode(415);
                    if (c.Request.ContentLength > MaximumBodyBytes) return Results.StatusCode(413);
                    using MemoryStream bytes = new(); byte[] buffer = new byte[2048];
                    while (true)
                    {
                        int remaining = MaximumBodyBytes - (int)bytes.Length;
                        int n = await c.Request.Body.ReadAsync(buffer.AsMemory(0, Math.Min(buffer.Length, remaining + 1)), ct);
                        if (n == 0) break; if (n > remaining) return Results.StatusCode(413); bytes.Write(buffer, 0, n);
                    }
                    AuctionCommandRequest r;
                    try
                    {
                        using var json = JsonDocument.Parse(bytes.ToArray(), new JsonDocumentOptions { MaxDepth = 12 });
                        TradeApiContracts.RejectDuplicateProperties(json.RootElement);
                        r = json.RootElement.Deserialize<AuctionCommandRequest>(TradeApiContracts.InputOptions) ?? throw new JsonException();
                    }
                    catch (JsonException) { throw AuctionApiContracts.Invalid(); }
                    AuctionApiContracts.Shape(r, action);
                    object result;
                    switch (action)
                    {
                        case "register":
                            if (r.ItemId is not { } item || item == Guid.Empty || r.Quantity is not > 0 || r.ExpiresAt is null) throw AuctionApiContracts.Invalid();
                            result = AuctionApiContracts.Result(await a.RegisterAsync(actor, listingId, r.RequestId, new(item, AuctionApiContracts.Integer(r.ExpectedItemRevision, true), r.Quantity.Value),
                                AuctionApiContracts.Integer(r.TotalPrice), r.ExpiresAt.Value, ct)); break;
                        case "purchase":
                            result = AuctionApiContracts.Result(await a.PurchaseAsync(new(r.RequestId, listingId, actor, AuctionApiContracts.Integer(r.ExpectedRevision, true), r.ExpectedCurrencyCode!, AuctionApiContracts.Integer(r.TotalPrice)), ct)); break;
                        case "cancel":
                            result = AuctionApiContracts.Result(await a.CancelAsync(actor, listingId, r.RequestId, AuctionApiContracts.Integer(r.ExpectedRevision, true), ct)); break;
                        default:
                            result = AuctionApiContracts.Result(await a.ClaimAsync(new(r.RequestId, listingId, actor, AuctionApiContracts.Integer(r.ExpectedRevision, true), r.ExpectedCurrencyCode!, AuctionApiContracts.Integer(r.RequestedAmount)), ct)); break;
                    }
                    return Results.Ok(result);
                }
                catch (TradeRuleException e) { return Failure(c, e.Code); }
            });
        }
    }
    private static async Task<IResult> Run(HttpContext c, Func<Task<object>> action)
    { c.Response.Headers.CacheControl = "no-store"; try { return Results.Ok(await action()); } catch (TradeRuleException e) { return Failure(c, e.Code); } }
    private static void NoQuery(HttpContext c) { if (c.Request.Query.Count != 0) throw new TradeRuleException("invalid_auction_query"); }
    private static IResult Failure(HttpContext c, string code)
    {
        int status = code switch
        {
            "auction_player_required" or "character_not_owned" => 403,
            "auction_not_found" or "auction_payable_not_found" or "auction_payable_owner_required" or "auction_seller_required" or "auctions_disabled" => 404,
            "invalid_auction_command" or "invalid_auction_purchase" or "invalid_auction_claim" or "invalid_auction_query" or "invalid_auction_price" or "invalid_auction_duration" => 400,
            "auction_rate_limit" => 429,
            _ => 409
        };
        if (status == 429) c.Response.Headers.RetryAfter = "60";
        return Results.Json(new { error = status == 404 && code != "auctions_disabled" ? "auction_not_found" : code }, statusCode: status);
    }
}
