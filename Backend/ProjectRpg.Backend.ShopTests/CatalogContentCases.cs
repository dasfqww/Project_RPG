using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Shop;

namespace ProjectRpg.Backend.ShopTests;

/// <summary>Packaged catalogue and production reward delivery -> shop HTTP. Test authentication only.</summary>
public static class CatalogContentCases
{
    public static async Task Run(ShopCatalog catalog, Action<bool, string> check)
    {
        var potion = catalog.Offers.Single();
        var junk = catalog.SaleRules.Single();
        check(catalog.DisplayName == "보급 상점" && potion.UnitPrice == 25
            && potion.MaximumQuantity == 10 && potion.MaximumStackSize == 10,
            "initial supply price is 25 gold per potion, at most ten per purchase");
        check(junk.RuleId == "broken-part" && junk.DisplayName == "망가진 부품"
            && junk.DefinitionType == "RPGItemDefinition" && junk.DefinitionName == "GameItem.Material.Junk.BrokenPart"
            && junk.DefinitionVersion == 1 && junk.UnitPrice == 5
            && junk.MaximumStackSize == 99 && junk.MaximumQuantity == 99
            && junk.AllowedBindStates.SequenceEqual([ItemBindState.Unbound]),
            "only the authored unbound broken part is bought for five gold");
        check(catalog.Offers.All(o => o.DefinitionName != junk.DefinitionName),
            "the merchant does not sell its own scrap for a buy/sell loop");

        var gate = new InMemoryTransactionGate();
        var items = new InMemoryItemRepository(gate);
        var economy = new InMemoryEconomyRepository(gate);
        var game = new InMemoryGameRepository(gate, items);
        var store = new InMemoryShopStore(gate, game, items, economy);
        var now = DateTimeOffset.UtcNow;
        await game.EnsureCreatedAsync(default);
        await economy.UpsertDefinitionAsync(new(catalog.CurrencyCode, "골드", CurrencyScope.Roster, long.MaxValue, true), now, default);
        const string account = "76561198000000725";
        await game.UpsertAccountAsync(new(account, "catalogue player", false, false), now, default);
        var character = await game.CreateCharacterAsync(account, "CatalogPlayer", now, default);
        var actor = new ShopActor(account, character.CharacterId);
        var owner = new ItemOwnerRef(ItemOwnerType.Character, actor.CharacterId.ToString("D"));
        var settlement = new DungeonRewardSettlement(Guid.NewGuid(), "catalogue-test", "pve_normal_v2", "catalogue-normal-v2",
            DungeonRewardSettlementState.Processing, [actor.CharacterId], [new(catalog.CurrencyCode, 250)],
            [Reward(potion.DefinitionName, 2, potion.MaximumStackSize), Reward(junk.DefinitionName, 2, junk.MaximumStackSize)],
            1, now, "catalogue-worker", now.AddMinutes(1), null, now, now, null);
        var batch = await DungeonRewardCommitPlanner.BuildAsync(settlement, game, default);
        var rewards = new InMemoryDungeonRewardCommitRepository(gate, economy, items);
        check((await rewards.CommitAsync(batch, now, default)).Succeeded, "normal-v2 reward is delivered atomically");
        check((await rewards.CommitAsync(batch, now, default)).Succeeded, "normal-v2 reward replay succeeds without another grant");
        var initial = await store.ReadAsync(actor, catalog, default);
        var scrap = initial.Items.Single(i => i.DefinitionName == junk.DefinitionName);
        check(initial.Balance.Balance == 250 && scrap.State.Quantity == 2
            && scrap.Location.ContainerType == ItemContainerType.Inventory && scrap.Location.SlotIndex >= 160,
            "normal reward reaches gold wallet and other-items bag before sale");
        check(initial.Items.Single(i => i.DefinitionName == potion.DefinitionName).State.Quantity == 2,
            "reward replay does not duplicate the two existing potion rewards");

        // These real progression identities must remain outside the merchant's exact allowlist.
        int slot = 161;
        foreach (var name in new[] { FirstSliceEquipmentDefinitions.OreDefinitionName, FirstSliceEquipmentDefinitions.TokenDefinitionName })
        {
            var record = scrap with { DefinitionName = name, Location = new(ItemContainerType.Inventory, owner.OwnerId, slot++),
                State = scrap.State with { InstanceId = Guid.NewGuid(), Quantity = 2 }, Revision = 0 };
            check((await items.CommitAsync(new(Guid.NewGuid(), "CatalogProtectionFixture", Guid.NewGuid().ToString("N"), owner,
                2, [new(0, record)]), now, default)).Status == ItemRepositoryCommitStatus.Committed,
                "progression protection fixture is stored");
        }

        var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing", ContentRootPath = AppContext.BaseDirectory });
        builder.Logging.ClearProviders();
        builder.Configuration["Kestrel:Endpoints:Http:Url"] = "http://127.0.0.1:0";
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(catalog);
        builder.Services.AddSingleton<IShopStore>(store);
        builder.Services.AddSingleton<ShopRequestLimiter>();
        await using var app = builder.Build();
        app.Use((context, next) =>
        {
            context.Items[ApiAuthenticationMiddleware.PrincipalItemKey] = new AuthenticatedPrincipal(PrincipalKind.Player, account, null);
            return next(context);
        });
        app.MapShopApi();
        await app.StartAsync();
        using var client = new HttpClient { BaseAddress = new(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
        var root = $"/api/characters/{actor.CharacterId:D}/shop";
        try
        {
            var snapshot = await Send(root);
            var rows = snapshot["saleItems"]!.AsArray();
            check(rows.Count(i => i!["canSell"]!.GetValue<bool>()) == 1,
                "HTTP exposes exactly one eligible item and protects equipment, potions and progression materials");
            var row = rows.Single(i => i!["itemId"]!.GetValue<Guid>() == scrap.ItemId)!;
            check(row["displayName"]!.GetValue<string>() == junk.DisplayName && row["unitPrice"]!.GetValue<string>() == "5"
                && row["maximumQuantity"]!.GetValue<int>() == 2, "HTTP advertises actual junk identity, owned quantity and price");
            var sale = new ShopCommand(Guid.NewGuid(), actor, catalog.ShopId, catalog.PolicyTag, "sell", junk.RuleId,
                2, junk.UnitPrice, scrap.ItemId, scrap.Revision);
            var sold = await Send(root + "/transactions", sale);
            check(sold["totalPrice"]!.GetValue<string>() == "10" && sold["balanceAfter"]!.GetValue<string>() == "260",
                "selling two normal-drop parts grants exactly ten gold");
            check(JsonNode.DeepEquals(sold, await Send(root + "/transactions", sale)), "sale replay returns the original receipt");
            var buy = new ShopCommand(Guid.NewGuid(), actor, catalog.ShopId, catalog.PolicyTag, "buy", potion.OfferId, 1, potion.UnitPrice);
            var bought = await Send(root + "/transactions", buy);
            check(bought["balanceAfter"]!.GetValue<string>() == "235", "one extra potion consumes exactly 25 gold");
            check(JsonNode.DeepEquals(bought, await Send(root + "/transactions", buy)), "purchase replay cannot charge twice");
            var after = await store.ReadAsync(actor, catalog, default);
            check(after.Balance.Balance == 235 && after.Items.All(i => i.ItemId != scrap.ItemId)
                && after.Items.Single(i => i.DefinitionName == potion.DefinitionName).State.Quantity == 3,
                "reward -> sale -> purchase leaves three potions, no scrap and 235 gold");
            check(after.Items.Count(i => i.DefinitionName is FirstSliceEquipmentDefinitions.OreDefinitionName
                or FirstSliceEquipmentDefinitions.TokenDefinitionName && i.State.Quantity == 2) == 2,
                "progression materials survive ordinary merchant transactions");
        }
        finally { await app.StopAsync(); }

        DungeonItemReward Reward(string name, int quantity, int stack) => new("RPGItemDefinition", name, 1, quantity,
            ItemBindState.Unbound, 0, 0, [], [], stack);
        async Task<JsonNode> Send(string path, ShopCommand? command = null)
        {
            using var request = new HttpRequestMessage(command is null ? HttpMethod.Get : HttpMethod.Post, path);
            if (command is not null)
                request.Content = new StringContent(JsonSerializer.Serialize(new { requestId = command.RequestId, shopId = command.ShopId,
                    policyTag = command.PolicyTag, action = command.Action, offerId = command.OfferId, quantity = command.Quantity,
                    expectedUnitPrice = ShopApiContracts.Number(command.ExpectedUnitPrice), itemId = command.ItemId,
                    expectedItemRevision = command.ExpectedItemRevision is { } revision ? ShopApiContracts.Number(revision) : null }), Encoding.UTF8, "application/json");
            using var response = await client.SendAsync(request);
            var body = await response.Content.ReadAsStringAsync();
            check(response.IsSuccessStatusCode, $"catalogue HTTP {request.Method} {path}: {(int)response.StatusCode} {body}");
            return JsonNode.Parse(body)!;
        }
    }
}
