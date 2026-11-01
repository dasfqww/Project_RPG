using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using static Tests;

static class PlayerLogoutTests
{
    public static async Task RunSuite(string provider, IGameRepository repository)
    {
        Fixture f = await Fixture.Create(repository);
        AccessTokenService tokens = new(new FixedClock(f.Now));
        var current = tokens.Issue(TimeSpan.FromHours(1));
        var sameAccount = tokens.Issue(TimeSpan.FromHours(1));
        var other = tokens.Issue(TimeSpan.FromHours(1));
        var server = tokens.Issue(TimeSpan.FromHours(1));
        var expired = tokens.Issue(TimeSpan.FromHours(1));
        await repository.StoreSessionAsync(current.TokenHash, f.A.SteamId, current.ExpiresAt, default);
        await repository.StoreSessionAsync(sameAccount.TokenHash, f.A.SteamId, sameAccount.ExpiresAt, default);
        await repository.StoreSessionAsync(other.TokenHash, f.B.SteamId, other.ExpiresAt, default);
        await repository.StoreSessionAsync(expired.TokenHash, f.A.SteamId, f.Now.AddMinutes(-1), default);
        await repository.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        await repository.RevokeSessionAsync(current.TokenHash, f.B.SteamId, default);
        Require(await repository.ResolveSessionAsync(current.TokenHash, f.Now, default) == f.A.SteamId,
            "Wrong owner revoked a player credential.");
        Console.WriteLine($"PASS {provider} logout: repository requires exact owner and token");
        using (CancellationTokenSource cancelled = new())
        {
            cancelled.Cancel();
            try { await repository.RevokeSessionAsync(current.TokenHash, f.A.SteamId, cancelled.Token);
                throw new TestFailure("Cancelled logout unexpectedly completed."); }
            catch (OperationCanceledException) { }
        }
        Require(await repository.ResolveSessionAsync(current.TokenHash, f.Now, default) == f.A.SteamId,
            "Pre-cancelled logout deleted a token.");
        Console.WriteLine($"PASS {provider} logout: pre-cancelled operation preserves credential");

        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(repository);
        builder.Services.AddSingleton<TimeProvider>(new FixedClock(f.Now));
        builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = "logout-admin-fixture" }));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapPlayerLogoutApi();
        await app.StartAsync();
        try
        {
            using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
            async Task<HttpResponseMessage> Logout(string? token, bool forgedTarget = false)
            {
                using HttpRequestMessage request = new(HttpMethod.Post, "/api/auth/logout");
                if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
                if (forgedTarget) request.Content = JsonContent.Create(new { steamId = f.B.SteamId, accessToken = other.Token });
                return await client.SendAsync(request);
            }
            using (var response = await Logout(null)) Require(response.StatusCode == HttpStatusCode.Unauthorized, "Anonymous logout accepted.");
            using (var response = await Logout(server.Token)) Require(response.StatusCode == HttpStatusCode.Forbidden, "Server logged out a player.");
            using (var response = await Logout("logout-admin-fixture")) Require(response.StatusCode == HttpStatusCode.Forbidden, "Admin used player logout.");
            Console.WriteLine($"PASS {provider} logout: anonymous, server and admin cannot revoke player tokens");
            using (var response = await Logout(current.Token, true))
                Require(response.StatusCode == HttpStatusCode.NoContent && response.Headers.CacheControl?.NoStore == true,
                    "Current player logout did not return an uncached acknowledgement.");
            Require(await repository.ResolveSessionAsync(current.TokenHash, f.Now, default) is null
                && await repository.ResolveSessionAsync(sameAccount.TokenHash, f.Now, default) == f.A.SteamId
                && await repository.ResolveSessionAsync(other.TokenHash, f.Now, default) == f.B.SteamId,
                "Logout affected another account/device credential or trusted the request body.");
            Require((await f.Read()).Members.Count == 2
                && await repository.ResolveGameServerCredentialAsync(server.TokenHash, f.Now, TimeSpan.Zero, default) is not null,
                "Player logout altered dungeon membership or its server credential.");
            Console.WriteLine($"PASS {provider} logout: only the current token is revoked; party and other devices survive");
            using (var response = await Logout(current.Token)) Require(response.StatusCode == HttpStatusCode.Unauthorized, "Revoked token remained usable.");
            using (var response = await Logout(expired.Token)) Require(response.StatusCode == HttpStatusCode.Unauthorized, "Expired token remained usable.");
            Console.WriteLine($"PASS {provider} logout: repeated and expired credentials remain unauthenticated");
        }
        finally { await app.StopAsync(); }
    }
}
