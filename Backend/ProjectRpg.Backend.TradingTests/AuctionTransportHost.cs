using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

// Owned loopback host for the native UE HTTP regression. No production test switches.
internal static class AuctionTransportHost
{
    private sealed class LostResponse(InMemoryAuctionRepository store) : IAuctionPurchaseRepository
    {
        private int attempts;
        public async Task<AuctionPurchaseResult> PurchaseAsync(AuctionPurchaseCommand c, CancellationToken ct)
        {
            var result = await store.PurchaseAsync(c, ct);
            if (Interlocked.Increment(ref attempts) == 1) throw new ServiceDrainingException();
            return result;
        }
    }
    public static async Task<int> Run(string directory)
    {
        string path = Path.GetFullPath(directory);
        if (!Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_AUCTION_HTTP_RUN"), "N", out _)
            || !path.Contains(Path.DirectorySeparatorChar + "Saved" + Path.DirectorySeparatorChar + "AH" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new Exception("Owned HTTP fixture path/run required");
        Directory.CreateDirectory(path);
        var clock = TimeProvider.System; var gate = new InMemoryTransactionGate(clock);
        var items = new InMemoryItemRepository(gate); var money = new InMemoryEconomyRepository(gate); var game = new InMemoryGameRepository(gate, items, clock);
        var options = new AuctionOptions { Enabled = true };
        var b = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing", ContentRootPath = AppContext.BaseDirectory });
        b.Logging.ClearProviders(); b.Configuration["Kestrel:Endpoints:Http:Url"] = "http://127.0.0.1:0";
        b.WebHost.UseUrls("http://127.0.0.1:0");
        b.Services.AddSingleton(gate); b.Services.AddSingleton(items); b.Services.AddSingleton(money);
        b.Services.AddSingleton<IGameRepository>(game); b.Services.AddSingleton<IItemRepository>(items); b.Services.AddSingleton<IEconomyRepository>(money);
        b.Services.AddSingleton<TimeProvider>(clock); b.Services.AddSingleton(new TradeItemPolicy([new("Material", "Ore", 1, true, true, 100)]));
        string admin = Guid.NewGuid().ToString("N");
        b.Services.AddSingleton<AccessTokenService>(); b.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
        b.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        b.Services.AddGameRepositoryContracts(); b.Services.AddAuctionServices(options, false); b.Services.RemoveAll<IHostedService>();
        b.Services.RemoveAll<IAuctionPurchaseRepository>(); b.Services.AddSingleton<IAuctionPurchaseRepository, LostResponse>(); b.Services.AddScoped<BearerAuthenticator>();
        await using var app = b.Build();
        var bodies = new List<string>(); var searches = new List<string>(); var statuses = new List<int>();
        Guid buyerId = Guid.Empty;
        async Task Stats()
        {
            // Hashes and counters only; never retain bearer material.
            var context = (await game.GetCharacterEconomyContextAsync(buyerId, default))!;
            var wallet = await money.LoadWalletAsync(context, default);
            var buyerItems = await items.FindByOwnerAsync(new(ItemOwnerType.Character, buyerId.ToString("D")), false, 500, default);
            await File.WriteAllTextAsync(Path.Combine(path, "stats.json"), JsonSerializer.Serialize(new { posts = bodies.ToArray(), searches = searches.ToArray(), statuses = statuses.ToArray(), balance = wallet.Balances.Single(x => x.Definition.CurrencyCode == "Gold").Balance, buyerQuantity = buyerItems.Where(x => x.DefinitionName == "Ore" && x.LifecycleState == ItemLifecycleState.Active).Sum(x => x.State.Quantity) }));
        }
        using var serial = new SemaphoreSlim(1);
        app.Use(async (c, next) =>
        {
            await serial.WaitAsync();
            try
            {
                if (c.Request.Method == "POST")
                {
                    c.Request.EnableBuffering(); using var reader = new StreamReader(c.Request.Body, Encoding.UTF8, leaveOpen: true);
                    var body = await reader.ReadToEndAsync(); c.Request.Body.Position = 0;
                    bodies.Add(Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(body))));
                }
                else searches.Add(c.Request.Path + c.Request.QueryString);
                await next(c); statuses.Add(c.Response.StatusCode); await Stats();
            }
            finally { serial.Release(); }
        });
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapAuctionApi();
        await game.EnsureCreatedAsync(default); var now = clock.GetUtcNow();
        await money.UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Account, 1000, true), now, default);
        async Task<(TradeParticipant Actor, string Token)> Person(string account)
        {
            await game.UpsertAccountAsync(new(account, account, false, false), now, default);
            var c = await game.CreateCharacterAsync(account, "Http" + account[^4..], now, default);
            var t = new AccessTokenService(clock).Issue(TimeSpan.FromHours(1)); await game.StoreSessionAsync(t.TokenHash, account, t.ExpiresAt, default);
            return (new(account, c.CharacterId), t.Token);
        }
        var seller = await Person("76561198880000101"); var buyer = await Person("76561198880000102"); buyerId = buyer.Actor.CharacterId;
        var fund = await money.CommitAsync(new(Guid.NewGuid(), buyerId, Guid.NewGuid(), "Test.Fund", "native-http", "test", [new("Gold", 1000)]), (await game.GetCharacterEconomyContextAsync(buyerId, default))!, now, default);
        if (fund.Status != CurrencyTransactionStatus.Committed) throw new Exception("Funding failed");
        var owner = new ItemOwnerRef(ItemOwnerType.Character, seller.Actor.CharacterId.ToString("D"));
        var raw = new ItemRecord("Material", "Ore", 1, owner, new(ItemContainerType.Inventory, owner.OwnerId, 160), new(Guid.NewGuid(), 17, 10, ["Item.Ore"], [new("Stat.Test", 4)]), 0, ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, "HttpFixture", false));
        var seed = await items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "http", owner, 10, [new(0, raw)]), now, default);
        if (seed.Status != ItemRepositoryCommitStatus.Committed) throw new Exception("Seeding failed");
        var store = app.Services.GetRequiredService<IAuctionEscrowRepository>(); Guid listing = Guid.NewGuid();
        await store.ExecuteAsync(new(Guid.NewGuid(), listing, seller.Actor, AuctionEscrowKind.Register, Item: new(raw.ItemId, 1, 4), TotalPrice: 699, ExpiresAt: now.AddHours(1), Terms: options.Terms()), default);
        await app.StartAsync();
        var endpoint = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single() + "/api";
        await File.WriteAllTextAsync(Path.Combine(path, "private.json"), JsonSerializer.Serialize(new { endpoint, account = buyer.Actor.AccountId, character = buyerId, token = buyer.Token, sellerAccount = seller.Actor.AccountId, sellerCharacter = seller.Actor.CharacterId, sellerToken = seller.Token, admin, listing }));
        Console.WriteLine("READY owned auction Memory HTTP host; credentials only in temporary private fixture.");
        while (!File.Exists(Path.Combine(path, "stop"))) await Task.Delay(100);
        await app.StopAsync(); await Stats(); return 0;
    }
}
