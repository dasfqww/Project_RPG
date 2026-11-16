using System.Net;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Shop;

namespace ProjectRpg.Backend.ShopTests;

/// <summary>Real loopback HTTP and production endpoints/storage; injected test principals are not Steam authentication evidence.</summary>
public static class ApiCases
{
    public static async Task Run(Action<bool, string> assert)
    {
        var f = await Fixture.Create(assert);
        ShopCatalog activeCatalog = f.Catalog;
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions
            { EnvironmentName = "Testing", ContentRootPath = AppContext.BaseDirectory });
        builder.Logging.ClearProviders();
        builder.Configuration["Kestrel:Endpoints:Http:Url"] = "http://127.0.0.1:0";
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IShopStore>(f.Store);
        builder.Services.AddSingleton<ShopRequestLimiter>();
        builder.Services.AddTransient<ShopCatalog>(_ => activeCatalog);
        await using var app = builder.Build();
        app.Use(async (context, next) =>
        {
            string account = context.Request.Headers["X-Shop-Test-Account"].ToString();
            string kind = context.Request.Headers["X-Shop-Test-Kind"].ToString();
            if (account.Length == 0)
            { context.Response.Headers.CacheControl = "no-store"; context.Response.StatusCode = 401; return; }
            context.Items[ApiAuthenticationMiddleware.PrincipalItemKey] = new AuthenticatedPrincipal(
                kind == "GameServer" ? PrincipalKind.GameServer : kind == "Administrator" ? PrincipalKind.Administrator : PrincipalKind.Player,
                account, kind == "GameServer" ? "fixture" : null, IsSecurityTelemetryOnly: kind == "Telemetry");
            await next(context);
        });
        app.MapShopApi();
        await app.StartAsync();
        using var client = new HttpClient
        { BaseAddress = new(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()), Timeout = TimeSpan.FromSeconds(20) };
        string Root(ShopActor actor) => $"/api/characters/{actor.CharacterId:D}/shop";
        string Body(ShopCommand c) => JsonSerializer.Serialize(new
        {
            requestId = c.RequestId, shopId = c.ShopId, policyTag = c.PolicyTag, action = c.Action, offerId = c.OfferId,
            quantity = c.Quantity, expectedUnitPrice = c.ExpectedUnitPrice.ToString(System.Globalization.CultureInfo.InvariantCulture),
            itemId = c.ItemId, expectedItemRevision = c.ExpectedItemRevision?.ToString(System.Globalization.CultureInfo.InvariantCulture)
        });
        async Task<JsonNode> Send(ShopActor actor, string path, int expected = 200, string? body = null,
            string media = "application/json", string kind = "Player", bool chunked = false)
        {
            using var request = new HttpRequestMessage(body is null ? HttpMethod.Get : HttpMethod.Post, path);
            request.Headers.Add("X-Shop-Test-Account", actor.AccountId); request.Headers.Add("X-Shop-Test-Kind", kind);
            if (body is not null)
            {
                request.Content = chunked ? new UnknownLengthContent(body) : new StringContent(body, Encoding.UTF8, media);
                if (chunked) request.Content.Headers.ContentType = new(media);
            }
            using var response = await client.SendAsync(request);
            string text = await response.Content.ReadAsStringAsync();
            assert((int)response.StatusCode == expected, $"HTTP {request.Method} {path}: expected {expected}, got {(int)response.StatusCode}: {text}");
            assert(response.Headers.CacheControl?.NoStore == true, "shop HTTP responses disable caches");
            return text.Length == 0 ? new JsonObject() : JsonNode.Parse(text)!;
        }
        async Task<JsonNode> Snapshot(ShopActor actor) => await Send(actor, Root(actor));
        void Rejection(JsonNode result, ShopCommand command, string error)
        {
            assert(result.AsObject().Count == 13 && result["rejected"]!.GetValue<bool>()
                && result["error"]!.GetValue<string>() == error
                && result["requestId"]!.GetValue<Guid>() == command.RequestId
                && result["characterId"]!.GetValue<Guid>() == command.Actor.CharacterId
                && result["shopId"]!.GetValue<string>() == command.ShopId
                && result["policyTag"]!.GetValue<string>() == command.PolicyTag
                && result["action"]!.GetValue<string>() == command.Action
                && result["offerId"]!.GetValue<string>() == command.OfferId
                && result["quantity"]!.GetValue<int>() == command.Quantity
                && result["expectedUnitPrice"]!.GetValue<string>() == command.ExpectedUnitPrice.ToString(System.Globalization.CultureInfo.InvariantCulture)
                && result["itemId"]?.GetValue<Guid>() == command.ItemId
                && result["expectedItemRevision"]?.GetValue<string>() == command.ExpectedItemRevision?.ToString(System.Globalization.CultureInfo.InvariantCulture)
                && result["rejectedAtUtc"]!.GetValue<DateTimeOffset>() != default,
                "HTTP terminal rejection proves the complete original command");
        }
        try
        {
            var buyer = await f.Person(); var junk = await f.Seed(buyer, Fixture.Junk.DefinitionName, 160);
            var snapshot = await Snapshot(buyer);
            assert(snapshot["balance"]!.GetValue<string>() == "1000" && snapshot["availableBalance"]!.GetValue<string>() == "1000"
                && snapshot["offers"]![0]!["maximumPurchasable"]!.GetValue<int>() == 100, "HTTP catalog reports exact funds and maximum purchase");
            assert(snapshot["inventoryFreeSlots"]![1]!.GetValue<int>() == 80 && snapshot["inventoryFreeSlots"]![2]!.GetValue<int>() == 79,
                "HTTP catalog reports free capacity per bag tab");
            var saleRow = snapshot["saleItems"]!.AsArray().Single(i => i!["itemId"]!.GetValue<Guid>() == junk.ItemId)!;
            assert(saleRow["canSell"]!.GetValue<bool>() && saleRow["maximumQuantity"]!.GetValue<int>() == 4
                && saleRow["unitPrice"]!.GetValue<string>() == "2", "HTTP sale row presents server allowlist and quantity");
            assert(snapshot["saleItems"]!.AsArray().Any(i => !i!["canSell"]!.GetValue<bool>() && i["reason"]!.GetValue<string>() == "shop_sale_not_allowed"),
                "HTTP exposes protected starter equipment as sale ineligible");

            var order = f.Buy(buyer); string requestBody = Body(order);
            var purchase = await Send(buyer, Root(buyer) + "/transactions", body: requestBody);
            assert(purchase["requestId"]!.GetValue<Guid>() == order.RequestId && purchase["quantity"]!.GetValue<int>() == 2
                && purchase["unitPrice"]!.GetValue<string>() == "10" && purchase["totalPrice"]!.GetValue<string>() == "20"
                && purchase["balanceAfter"]!.GetValue<string>() == "980", "HTTP purchase has correlated exact receipt");
            var replay = await Send(buyer, Root(buyer) + "/transactions", body: requestBody);
            assert(JsonNode.DeepEquals(purchase, replay) && await f.Balance(buyer) == 980, "HTTP replay leaves original receipt and balance");
            var queried = await Send(buyer, Root(buyer) + $"/transactions/{order.RequestId:D}");
            assert(JsonNode.DeepEquals(purchase, queried), "HTTP request status returns original receipt");
            var assets = await Send(buyer, Root(buyer) + $"/transactions/{order.RequestId:D}/assets");
            assert(assets["isComplete"]!.GetValue<bool>() && assets["requestId"]!.GetValue<Guid>() == order.RequestId
                && assets["characterId"]!.GetValue<Guid>() == buyer.CharacterId && assets["items"]!.AsArray().Count > 0,
                "HTTP assets are complete and correlated to character and request");
            var sale = f.Sell(buyer, junk); var sold = await Send(buyer, Root(buyer) + "/transactions", body: Body(sale));
            assert(sold["action"]!.GetValue<string>() == "sell" && sold["itemId"]!.GetValue<Guid>() == junk.ItemId
                && sold["balanceAfter"]!.GetValue<string>() == "984" && (await f.Items.FindAsync(junk.ItemId, default))!.State.Quantity == 2,
                "HTTP sale consumes exact amount and credits correct wallet");

            var other = await f.Person();
            await Send(other, Root(buyer), 403);
            await Send(other, Root(other) + $"/transactions/{order.RequestId:D}", 404);
            await Send(other, Root(other) + $"/transactions/{order.RequestId:D}/assets", 404);
            await Send(buyer, Root(buyer), 403, kind: "GameServer");
            await Send(buyer, Root(buyer), 403, kind: "Administrator");
            await Send(buyer, Root(buyer), 403, kind: "Telemetry");
            await Send(buyer, Root(buyer) + "?characterId=" + other.CharacterId, 400);
            await Send(buyer, Root(buyer) + $"/transactions/{Guid.NewGuid():D}", 404);

            string before = JsonSerializer.Serialize((await f.Owned(buyer)).OrderBy(i => i.ItemId).ToArray());
            long beforeMoney = await f.Balance(buyer);
            var malformedOrder = f.Buy(buyer);
            string valid = Body(malformedOrder); string post = Root(buyer) + "/transactions";
            await Send(buyer, post, 400, valid[..^1] + ",\"quantity\":2}");
            var extra = JsonNode.Parse(valid)!; extra["walletOwner"] = other.CharacterId.ToString("D");
            await Send(buyer, post, 400, extra.ToJsonString());
            var numericMoney = JsonNode.Parse(valid)!; numericMoney["expectedUnitPrice"] = 10;
            await Send(buyer, post, 400, numericMoney.ToJsonString());
            var floatMoney = JsonNode.Parse(valid)!; floatMoney["expectedUnitPrice"] = "10.0";
            await Send(buyer, post, 400, floatMoney.ToJsonString());
            var leadingZero = JsonNode.Parse(valid)!; leadingZero["expectedUnitPrice"] = "010";
            await Send(buyer, post, 400, leadingZero.ToJsonString());
            var fraction = JsonNode.Parse(valid)!; fraction["quantity"] = 1.5;
            await Send(buyer, post, 400, fraction.ToJsonString());
            var zero = JsonNode.Parse(valid)!; zero["quantity"] = 0;
            await Send(buyer, post, 400, zero.ToJsonString());
            await Send(buyer, post, 400, "{");
            await Send(buyer, post, 400, "[]");
            await Send(buyer, post, 415, valid, "text/plain");
            string huge = valid[..^1] + ",\"oversized\":\"" + new string('x', 5000) + "\"}";
            await Send(buyer, post, 413, huge);
            await Send(buyer, post, 413, huge, chunked: true);
            assert(beforeMoney == await f.Balance(buyer)
                && before == JsonSerializer.Serialize((await f.Owned(buyer)).OrderBy(i => i.ItemId).ToArray()),
                "malformed and oversized HTTP commands have no asset effects");
            await Send(buyer, Root(buyer) + $"/transactions/{malformedOrder.RequestId:D}", 404);
            var tampered = order with { Quantity = 1 };
            var conflict = await Send(buyer, post, 409, Body(tampered));
            assert(conflict["error"]!.GetValue<string>() == "shop_idempotency_conflict", "HTTP altered replay is definitive id conflict");

            var reserved = await f.Person(); var reservedJunk = await f.Seed(reserved, Fixture.Junk.DefinitionName, 160);
            var hold = f.Reservation(reserved, 990, reservedJunk); await f.Reservations.ReserveAsync(hold, default);
            var heldSnapshot = await Snapshot(reserved);
            assert(heldSnapshot["availableBalance"]!.GetValue<string>() == "10"
                && heldSnapshot["offers"]![0]!["maximumPurchasable"]!.GetValue<int>() == 1, "HTTP maximum excludes reserved funds");
            var heldRow = heldSnapshot["saleItems"]!.AsArray().Single(i => i!["itemId"]!.GetValue<Guid>() == reservedJunk.ItemId)!;
            assert(!heldRow["canSell"]!.GetValue<bool>() && heldRow["reason"]!.GetValue<string>() == "shop_item_unavailable", "HTTP cannot sell reserved item");
            await f.Reservations.ReleaseAsync(hold, default);

            var packed = await f.Person(); ItemRecord? room = null;
            foreach (int slot in Enumerable.Range(80, 80))
            {
                var item = await f.Seed(packed, Fixture.Potion.DefinitionName, slot, slot == 80 ? 98 : 100);
                if (slot == 80) room = item;
            }
            var packedSnapshot = await Snapshot(packed);
            assert(packedSnapshot["inventoryFreeSlots"]![1]!.GetValue<int>() == 0
                && packedSnapshot["offers"]![0]!["maximumPurchasable"]!.GetValue<int>() == 2, "HTTP maximum includes compatible stack headroom with no free slots");
            var stackHold = f.Reservation(packed, item: room); await f.Reservations.ReserveAsync(stackHold, default);
            assert((await Snapshot(packed))["offers"]![0]!["maximumPurchasable"]!.GetValue<int>() == 0,
                "HTTP maximum excludes reserved stack headroom");
            await f.Reservations.ReleaseAsync(stackHold, default);

            await f.Economy.UpsertDefinitionAsync(new("ShopGold", "상점 시험 재화", CurrencyScope.Character, long.MaxValue, true), f.Now, default);
            const long exact = 9_007_199_254_740_993;
            var wide = await f.Person(exact);
            assert((await Snapshot(wide))["balance"]!.GetValue<string>() == "9007199254740993", "HTTP balance preserves integers beyond double precision");

            activeCatalog = new(true, f.Catalog.ShopId, "가격 변경", f.Catalog.CurrencyCode, [Fixture.Potion with { UnitPrice = 11 }], [Fixture.Junk]);
            var staleOrder = f.Buy(buyer);
            var stale = await Send(buyer, post, 409, Body(staleOrder));
            Rejection(stale, staleOrder, "shop_policy_changed");
            assert(JsonNode.DeepEquals(stale, await Send(buyer, Root(buyer) + $"/transactions/{staleOrder.RequestId:D}", 409)),
                "HTTP lookup resolves a lost policy rejection response");
            assert(JsonNode.DeepEquals(purchase, await Send(buyer, post, body: requestBody)), "HTTP historical replay survives policy change");
            activeCatalog = f.Catalog;
            assert(JsonNode.DeepEquals(stale, await Send(buyer, post, 409, Body(staleOrder)))
                && JsonNode.DeepEquals(stale, await Send(buyer, Root(buyer) + $"/transactions/{staleOrder.RequestId:D}/assets", 409)),
                "HTTP delayed original remains rejected even after original policy returns");
            await Send(other, Root(other) + $"/transactions/{staleOrder.RequestId:D}", 404);
            activeCatalog = new(false, "", "", "", [], []);
            await Send(buyer, Root(buyer), 404);
            var disabledOrder = f.Buy(buyer);
            var disabledResult = await Send(buyer, post, 409, Body(disabledOrder));
            Rejection(disabledResult, disabledOrder, "shop_disabled");
            assert(JsonNode.DeepEquals(disabledResult, await Send(buyer, Root(buyer) + $"/transactions/{disabledOrder.RequestId:D}", 409)),
                "HTTP disabled rejection is recoverable independently of catalog availability");
            assert(JsonNode.DeepEquals(purchase, await Send(buyer, Root(buyer) + $"/transactions/{order.RequestId:D}"))
                && JsonNode.DeepEquals(purchase, await Send(buyer, post, body: requestBody)), "HTTP receipt lookup and replay remain available when disabled");
            await Send(buyer, Root(buyer) + $"/transactions/{order.RequestId:D}/assets");
            activeCatalog = f.Catalog;
            assert(JsonNode.DeepEquals(disabledResult, await Send(buyer, post, 409, Body(disabledOrder))),
                "HTTP disabled rejection never changes into a purchase after activation");
            var blockedSale = f.Sell(buyer, junk);
            var blockedSaleResult = await Send(buyer, post, 409, Body(blockedSale));
            Rejection(blockedSaleResult, blockedSale, "shop_item_revision_conflict");
        }
        finally { await app.StopAsync(); }
    }

    private sealed class UnknownLengthContent(string text) : HttpContent
    {
        protected override bool TryComputeLength(out long length) { length = 0; return false; }
        protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context) =>
            stream.WriteAsync(Encoding.UTF8.GetBytes(text)).AsTask();
    }
}
