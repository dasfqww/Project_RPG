using System.Text.Json;
using System.Threading.RateLimiting;
using Npgsql;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Shop;

public sealed class ShopRequestLimiter : IAsyncDisposable
{
    private readonly PartitionedRateLimiter<string> limiter = PartitionedRateLimiter.Create<string, string>(key =>
        RateLimitPartition.GetFixedWindowLimiter(key, _ => new FixedWindowRateLimiterOptions
        { PermitLimit = 120, Window = TimeSpan.FromMinutes(1), QueueLimit = 0, AutoReplenishment = true }));
    public bool Acquire(string account) { using var lease = limiter.AttemptAcquire(account); return lease.IsAcquired; }
    public ValueTask DisposeAsync() => limiter.DisposeAsync();
}

public static class ShopApiEndpoints
{
    private const string Root = "/api/characters/{characterId:guid}/shop";
    private const int MaximumBodyBytes = 4096;
    public static void MapShopApi(this WebApplication app)
    {
        app.MapGet(Root, (Guid characterId, HttpContext c, IShopStore store, ShopCatalog catalog, ShopRequestLimiter limiter, CancellationToken ct) =>
            Run(c, characterId, limiter, async actor => ShopApiContracts.Snapshot(catalog, await store.ReadAsync(actor, catalog, ct))));
        app.MapGet(Root + "/transactions/{requestId:guid}", (Guid characterId, Guid requestId, HttpContext c, IShopStore store, ShopRequestLimiter limiter, CancellationToken ct) =>
            Run(c, characterId, limiter, async actor => await store.FindAsync(actor, requestId, ct) is { } receipt
                ? ShopApiContracts.Receipt(receipt) : Results.NotFound(new { error = "shop_receipt_not_found" })));
        app.MapGet(Root + "/transactions/{requestId:guid}/assets", (Guid characterId, Guid requestId, HttpContext c, IShopStore store, ShopRequestLimiter limiter, CancellationToken ct) =>
            Run(c, characterId, limiter, async actor => await store.AssetsAsync(actor, requestId, ct) is { } assets
                ? ShopApiContracts.Assets(assets) : Results.NotFound(new { error = "shop_receipt_not_found" })));
        app.MapPost(Root + "/transactions", (Guid characterId, HttpContext c, IShopStore store, ShopCatalog catalog, ShopRequestLimiter limiter, CancellationToken ct) =>
            Run(c, characterId, limiter, async actor =>
            {
                if (!c.Request.HasJsonContentType()) return Results.StatusCode(415);
                if (c.Request.ContentLength > MaximumBodyBytes) return Results.StatusCode(413);
                using var bytes = new MemoryStream(); byte[] buffer = new byte[1024];
                while (true)
                {
                    int remaining = MaximumBodyBytes - (int)bytes.Length;
                    int read = await c.Request.Body.ReadAsync(buffer.AsMemory(0, Math.Min(buffer.Length, remaining + 1)), ct);
                    if (read == 0) break;
                    if (read > remaining) return Results.StatusCode(413);
                    bytes.Write(buffer, 0, read);
                }
                ShopCommand command;
                try
                {
                    using var json = JsonDocument.Parse(bytes.ToArray(), new JsonDocumentOptions { MaxDepth = 8 });
                    command = ShopApiContracts.Command(json.RootElement, actor);
                }
                catch (Exception e) when (e is JsonException or InvalidOperationException or FormatException)
                { throw new ShopRuleException("invalid_shop_command"); }
                return ShopApiContracts.Receipt(await store.ExecuteAsync(command, catalog, ct));
            }));
    }
    private static async Task<IResult> Run(HttpContext c, Guid characterId, ShopRequestLimiter limiter, Func<ShopActor, Task<object>> action)
    {
        c.Response.Headers.CacheControl = "no-store";
        var principal = c.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null || principal.IsSecurityTelemetryOnly)
            return Results.Json(new { error = "shop_player_required" }, statusCode: 403);
        if (c.Request.Query.Count != 0) return Results.BadRequest(new { error = "invalid_shop_query" });
        if (!limiter.Acquire(principal.SteamId))
        { c.Response.Headers.RetryAfter = "60"; return Results.Json(new { error = "shop_rate_limit" }, statusCode: 429); }
        try { var result = await action(new(principal.SteamId, characterId)); return result is IResult response ? response : Results.Ok(result); }
        catch (ShopRejectedException e) { return Results.Json(ShopApiContracts.Rejection(e), statusCode: 409); }
        catch (ShopRuleException e)
        {
            int code = e.Code switch { "shop_character_not_owned" => 403, "shop_disabled" => 404,
                var s when s.StartsWith("invalid_", StringComparison.Ordinal) => 400, _ => 409 };
            return Results.Json(new { error = e.Code }, statusCode: code);
        }
        catch (TradeRuleException) { return Results.Json(new { error = "shop_asset_conflict" }, statusCode: 409); }
        catch (AccountRestrictedException) { return Results.Json(new { error = "account_restricted" }, statusCode: 403); }
        catch (ServiceDrainingException)
        { c.Response.Headers.RetryAfter = "30"; return Results.Json(new { error = "service_draining" }, statusCode: 503); }
        catch (Exception e) when (e is NpgsqlException or TimeoutException or InvalidDataException)
        { c.Response.Headers.RetryAfter = "1"; return Results.Json(new { error = "shop_storage_unavailable" }, statusCode: 503); }
    }
}
