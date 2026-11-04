using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static EquipmentLobbyCases;
using static EquipmentCraftingCases;

internal static class EquipmentCraftingHttpCases
{
    internal static async Task RunAsync(List<string> groups)
    {
        InMemoryTransactionGate gate = new();
        InMemoryItemRepository items = new(gate);
        InMemoryGameRepository game = new(gate, items);
        CraftFixture f = await NewFixture(game, items);
        AccessTokenService tokens = new(TimeProvider.System);
        IssuedAccessToken player = tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreSessionAsync(player.TokenHash, f.Steam, player.ExpiresAt, default);
        string admin = Guid.NewGuid().ToString("N");
        WebApplicationBuilder builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing", ContentRootPath = AppContext.BaseDirectory });
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IGameRepository>(game);
        builder.Services.AddSingleton<IEquipmentProgressionRepository>(game);
        builder.Services.AddSingleton<IItemRepository>(items);
        builder.Services.AddSingleton(TimeProvider.System);
        builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        builder.Services.AddSingleton<ItemTransactionService>();
        builder.Services.AddGameRepositoryContracts();
        builder.Services.AddSingleton<ItemOwnerAccessService>();
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>();
        app.MapEquipmentProgressionApi();
        app.MapItemApi();
        await app.StartAsync();
        try
        {
            string address = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single();
            using HttpClient client = new() { BaseAddress = new(address), Timeout = TimeSpan.FromSeconds(10) };
            string route = $"/api/characters/{f.Character.CharacterId:D}/equipment/";
            Guid requestId = Guid.NewGuid();
            object payload = new { requestId, ruleVersion = 1, targetDefinitionName = Target, targetDefinitionVersion = 1,
                tokenStacks = new[] { new { itemId = f.Token.ItemId, expectedRevision = "1" } } };
            await Status(client, route + "craft", payload, 401);
            await Status(client, route + "dismantle", new { }, 401);
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", admin);
            await Status(client, route + "craft", payload, 403);
            await Status(client, route + "dismantle", new { }, 403);
            client.DefaultRequestHeaders.Authorization = new("Bearer", player.Token);
            await Status(client, $"/api/characters/{Guid.NewGuid():D}/equipment/craft", payload, 403);
            await Status(client, "/api/item-transactions/commit", new { }, 403);
            JsonObject response;
            using (HttpResponseMessage success = await client.PostAsJsonAsync(route + "craft", payload))
            {
                Require((int)success.StatusCode == 200 && success.Headers.CacheControl?.NoStore == true, "HTTP craft failed/cacheable.");
                response = (await success.Content.ReadFromJsonAsync<JsonObject>())!;
            }
            JsonNode gear = response["records"]!.AsArray().Single(x => x!["definitionName"]!.GetValue<string>() == Target)!;
            Require(gear["state"]!["equipmentProgression"]!["level"]!.GetValue<int>() == 0, "HTTP craft omitted +0.");
            // Read identity from the authoritative response, never construct result records in the request.
            string id = gear["state"]!["instanceId"]!.GetValue<string>();
            object dismantle = new { requestId = Guid.NewGuid(), ruleVersion = 1,
                equipment = new { itemId = id, expectedRevision = "1", expectedLevel = 0 } };
            await Status(client, route + "dismantle", dismantle, 200);
            await game.CreateDungeonSessionAsync(f.Steam, f.Character.CharacterId, "CraftHttp", "Normal", Now, Now.AddHours(1), default);
            using (HttpResponseMessage replay = await client.PostAsJsonAsync(route + "craft", payload))
                Require((int)replay.StatusCode == 200 && (await replay.Content.ReadFromJsonAsync<JsonObject>())!["status"]!.GetValue<string>() == "AlreadyCommitted", "HTTP craft replay blocked by consumed output/lease.");
            await Status(client, route + "dismantle", dismantle, 200);
            await Status(client, route + "dismantle", new { requestId = Guid.NewGuid(), ruleVersion = 1,
                equipment = new { itemId = id, expectedRevision = "2", expectedLevel = 0 } }, 409);
            groups.Add("bearer_craft_dismantle_player_owner_only_receipt_replay_before_lease_and_generic_commit_denied");
            await Status(client, route + "craft", new { requestId = Guid.NewGuid(), ruleVersion = 1,
                targetDefinitionName = Target, targetDefinitionVersion = 1, tokenStacks = new[] { new { itemId = f.Token.ItemId, expectedRevision = 1 } } }, 400);
            await Status(client, route + "dismantle", new { requestId = Guid.NewGuid(), ruleVersion = 1,
                equipment = new { itemId = id, expectedRevision = "1" } }, 400);
            foreach (string operation in new[] { "craft", "dismantle" })
            {
                using HttpResponseMessage bad = await client.PostAsync(route + operation, new StringContent("{", Encoding.UTF8, "application/json"));
                using HttpResponseMessage media = await client.PostAsync(route + operation, new StringContent("{}"));
                using HttpResponseMessage big = await client.PostAsync(route + operation, new StringContent(new string(' ', EquipmentProgressionApiEndpoints.MaximumBodyBytes + 1), Encoding.UTF8, "application/json"));
                Require((int)bad.StatusCode == 400 && (int)media.StatusCode == 415 && (int)big.StatusCode == 413, "HTTP body bounds failed.");
            }
            groups.Add("craft_dismantle_required_fields_revision_strings_content_type_and_16KiB_limit");
        }
        finally { await app.StopAsync(); }
    }

    private static async Task Status(HttpClient client, string route, object payload, int status)
    {
        using HttpResponseMessage response = await client.PostAsJsonAsync(route, payload);
        Require((int)response.StatusCode == status, $"Expected {status}, got {(int)response.StatusCode}: {await response.Content.ReadAsStringAsync()}");
    }
    private static void Require(bool condition, string message) => EquipmentLobbyCases.Require(condition, message);
}
