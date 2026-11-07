using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

/// <summary>Actual item endpoints/JSON over loopback; principal injection is a test fixture, not authentication proof.</summary>
internal sealed class HttpFixture(WebApplication app, HttpClient client) : IAsyncDisposable
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { Converters = {new JsonStringEnumConverter()} };
    public static async Task<HttpFixture> Start(IItemRepository repository)
    {
        var builder = WebApplication.CreateBuilder();
        builder.Logging.ClearProviders();
        builder.WebHost.ConfigureKestrel(options => options.Listen(IPAddress.Loopback, 0));
        builder.Services.AddSingleton(repository);
        builder.Services.AddSingleton(TimeProvider.System);
        builder.Services.AddSingleton<ItemTransactionService>();
        builder.Services.AddSingleton<ICharacterOwnershipRepository, NoOwnership>();
        builder.Services.AddSingleton<ItemOwnerAccessService>();
        var app = builder.Build();
        app.Use(async (context, next) =>
        {
            context.Items[ApiAuthenticationMiddleware.PrincipalItemKey] =
                new AuthenticatedPrincipal(context.Request.Headers.ContainsKey("X-Test-Player") ? PrincipalKind.Player : PrincipalKind.GameServer,
                    "76561198000000001", "inventory-test");
            await next(context);
        });
        app.MapItemApi();
        await app.StartAsync();
        var address = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single();
        return new(app, new HttpClient {BaseAddress = new Uri(address)});
    }
    public async Task<ItemRepositoryCommitResult> Commit(ItemRepositoryCommitRequest request)
    {
        using var response = await client.PostAsJsonAsync("/api/item-transactions/commit", request, Json);
        if (response.StatusCode != HttpStatusCode.OK && response.StatusCode != HttpStatusCode.Conflict)
            throw new Exception("Unexpected item HTTP status: " + response.StatusCode + " " + await response.Content.ReadAsStringAsync());
        return (await response.Content.ReadFromJsonAsync<ItemRepositoryCommitResult>(Json))!;
    }
    public async Task<bool> RejectPlayerAndOrdinaryLargeCommit(ItemRepositoryCommitRequest request)
    {
        using var player = new HttpRequestMessage(HttpMethod.Post, "/api/item-transactions/commit");
        player.Headers.Add("X-Test-Player", "1");
        player.Content = JsonContent.Create(request, options: Json);
        using var denied = await client.SendAsync(player);
        using var large = await client.PostAsJsonAsync("/api/item-transactions/commit", request with {Operation = "Item.Move"}, Json);
        return denied.StatusCode == HttpStatusCode.Forbidden && large.StatusCode == HttpStatusCode.BadRequest;
    }
    public async ValueTask DisposeAsync() { client.Dispose(); await app.StopAsync(); await app.DisposeAsync(); }
    private sealed class NoOwnership : ICharacterOwnershipRepository
    { public Task<string?> GetCharacterOwnerAsync(Guid id, CancellationToken cancellation) => Task.FromResult<string?>(null); }
}
