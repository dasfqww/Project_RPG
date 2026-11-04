using System.Net;
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

internal static class EquipmentLobbyHttpCases
{
    internal static async Task RunAsync(List<string> groups)
    {
        InMemoryTransactionGate gate = new();
        InMemoryItemRepository items = new(gate);
        InMemoryGameRepository game = new(gate, items);
        Fixture f = await NewFixtureAsync(game, items);
        TimeProvider clock = TimeProvider.System;
        AccessTokenService tokens = new(clock);
        IssuedAccessToken player = tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreSessionAsync(player.TokenHash, f.Steam, player.ExpiresAt, default);
        string admin = Guid.NewGuid().ToString("N");
        WebApplicationBuilder builder = WebApplication.CreateBuilder(new WebApplicationOptions
            { EnvironmentName = "Testing", ContentRootPath = AppContext.BaseDirectory });
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IGameRepository>(game);
        builder.Services.AddSingleton<IEquipmentProgressionRepository>(game);
        builder.Services.AddSingleton<IItemRepository>(items);
        builder.Services.AddSingleton(clock);
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
            string route = $"/api/characters/{f.Character.CharacterId:D}/equipment/upgrade";
            Guid requestId = Guid.NewGuid();
            object payload = new { requestId, ruleVersion = 1,
                equipment = new { itemId = f.Equipment.ItemId, expectedRevision = "1", expectedLevel = 0 },
                oreStacks = new[] { new { itemId = f.Ore.ItemId, expectedRevision = "1" } } };
            await Status(client, route, payload, 401);
            client.DefaultRequestHeaders.Authorization = new("Bearer", admin);
            await Status(client, route, payload, 403);
            client.DefaultRequestHeaders.Authorization = new("Bearer", player.Token);
            await Status(client, $"/api/characters/{Guid.NewGuid():D}/equipment/upgrade", payload, 403);
            await Status(client, "/api/item-transactions/commit", new { }, 403);
            using (HttpResponseMessage success = await client.PostAsJsonAsync(route, payload))
            {
                Require(success.StatusCode == HttpStatusCode.OK && success.Headers.CacheControl?.NoStore == true, "Authenticated HTTP upgrade failed/cacheable.");
                JsonObject response = (await success.Content.ReadFromJsonAsync<JsonObject>())!;
                Require(response["status"]!.GetValue<string>() == "Committed"
                    && response["records"]![0]!["state"]!["equipmentProgression"]!["level"]!.GetValue<int>() == 1, "HTTP lost persisted growth.");
            }
            await game.CreateDungeonSessionAsync(f.Steam, f.Character.CharacterId, "EquipmentFixture", "Normal",
                DateTimeOffset.UtcNow, DateTimeOffset.UtcNow.AddMinutes(5), default);
            using (HttpResponseMessage replay = await client.PostAsJsonAsync(route, payload))
                Require(replay.StatusCode == HttpStatusCode.OK && (await replay.Content.ReadFromJsonAsync<JsonObject>())!["status"]!.GetValue<string>() == "AlreadyCommitted",
                    "HTTP exact replay was blocked by active lease.");
            await Status(client, route, new { requestId = Guid.NewGuid(), ruleVersion = 1,
                equipment = new { itemId = f.Equipment.ItemId, expectedRevision = "2", expectedLevel = 1 },
                oreStacks = new[] { new { itemId = f.Ore.ItemId, expectedRevision = "2" } } }, 409);
            groups.Add("real_bearer_http_owner_player_only_generic_commit_denied_and_lease_replay");

            await Status(client, route, new { requestId = Guid.NewGuid(), ruleVersion = 1, equipment = new { itemId = f.Equipment.ItemId, expectedRevision = "2" }, oreStacks = new object[0] }, 400);
            await Status(client, route, new { requestId = Guid.NewGuid(), ruleVersion = 1, equipment = new { itemId = f.Equipment.ItemId, expectedRevision = "9223372036854775808", expectedLevel = 1 }, oreStacks = new[] { new { itemId = f.Ore.ItemId, expectedRevision = "2" } } }, 400);
            using (HttpResponseMessage bad = await client.PostAsync(route, new StringContent("{", Encoding.UTF8, "application/json"))) Require((int)bad.StatusCode == 400, "Bad JSON accepted.");
            using (HttpResponseMessage media = await client.PostAsync(route, new StringContent("{}"))) Require((int)media.StatusCode == 415, "Unsupported content type accepted.");
            using (HttpResponseMessage big = await client.PostAsync(route, new StringContent(new string(' ', EquipmentProgressionApiEndpoints.MaximumBodyBytes + 1), Encoding.UTF8, "application/json"))) Require((int)big.StatusCode == 413, "Oversized request accepted.");
            groups.Add("http_required_fields_int64_strings_json_content_type_and_body_limit");

            Fixture transfer = await NewFixtureAsync(game, items);
            IssuedAccessToken second = tokens.Issue(TimeSpan.FromMinutes(5));
            await game.StoreSessionAsync(second.TokenHash, transfer.Steam, second.ExpiresAt, default);
            client.DefaultRequestHeaders.Authorization = new("Bearer", second.Token);
            await game.UpgradeEquipmentAsync(transfer.Steam, Upgrade(transfer), DateTimeOffset.UtcNow, default);
            string transferRoute = $"/api/characters/{transfer.Character.CharacterId:D}/equipment/transfer";
            await Status(client, transferRoute, new { requestId = Guid.NewGuid(), ruleVersion = 1,
                source = new { itemId = transfer.Equipment.ItemId, expectedRevision = "2", expectedLevel = 1 },
                target = new { itemId = transfer.Target.ItemId, expectedRevision = "1", expectedLevel = 0 } }, 200);
            Require((await items.FindAsync(transfer.Equipment.ItemId, default))!.State.EquipmentProgression!.Level == 0
                && (await items.FindAsync(transfer.Target.ItemId, default))!.State.EquipmentProgression!.Level == 1, "HTTP transfer did not persist both items.");
            groups.Add("http_transfer_persists_both_levels");
        }
        finally { await app.StopAsync(); }
    }

    private static async Task Status(HttpClient client, string route, object payload, int status)
    {
        using HttpResponseMessage response = await client.PostAsJsonAsync(route, payload);
        Require((int)response.StatusCode == status, $"HTTP expected {status}, got {(int)response.StatusCode} on {route}.");
    }

    private static void Require(bool condition, string message) => EquipmentLobbyCases.Require(condition, message);
}
