using System.Net;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Domain;

return await RunAsync();

static async Task<int> RunAsync()
{
    try
    {
        ApiRateLimitOptions options = new()
        {
            Enabled = true,
            WindowSeconds = 1,
            AuthenticationPermitLimit = 2,
            IngressPermitLimit = 12,
            PlayerPermitLimit = 2,
            GameServerPermitLimit = 3,
            AdministratorPermitLimit = 2
        };
        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton<IOptions<ApiRateLimitOptions>>(
            Options.Create(options));
        builder.Services.AddSingleton<ApiRateLimiter>();

        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiIngressRateLimitMiddleware>();
        app.Use(FakeAuthenticationAsync);
        app.UseMiddleware<AuthenticatedApiRateLimitMiddleware>();
        app.MapGet("/health", (HttpContext context) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            return Results.Ok(new { status = "ok" });
        });
        app.MapPost("/api/auth/steam-ticket", () => Results.Ok());
        app.MapGet(
            "/api/test",
            (HttpContext context) => Results.Ok(new
            {
                kind = context.GetProjectRpgPrincipal().Kind.ToString()
            }));
        await app.StartAsync();

        using HttpClient client = new()
        {
            BaseAddress = new Uri(app.Urls.Single()),
            Timeout = TimeSpan.FromSeconds(5)
        };
        try
        {
            await ExpectAsync(client, HttpMethod.Post, "/api/auth/steam-ticket", null, 200);
            await ExpectAsync(client, HttpMethod.Post, "/api/auth/steam-ticket", null, 200);
            await ExpectRateLimitedAsync(
                client,
                HttpMethod.Post,
                "/api/auth/steam-ticket",
                null);
            await ExpectAsync(client, HttpMethod.Get, "/health", null, 200);
            await WaitForResetAsync();
            await ExpectAsync(client, HttpMethod.Post, "/api/auth/steam-ticket", null, 200);
            Console.WriteLine("PASS authentication is limited per source address and health bypasses limits.");

            await ExpectAsync(client, HttpMethod.Get, "/api/test", "player-a", 200);
            await ExpectAsync(client, HttpMethod.Get, "/api/test", "player-a", 200);
            await ExpectRateLimitedAsync(client, HttpMethod.Get, "/api/test", "player-a");
            await ExpectAsync(client, HttpMethod.Get, "/api/test", "player-b", 200);

            await ExpectAsync(client, HttpMethod.Get, "/api/test", "administrator", 200);
            await ExpectAsync(client, HttpMethod.Get, "/api/test", "administrator", 200);
            await ExpectRateLimitedAsync(
                client,
                HttpMethod.Get,
                "/api/test",
                "administrator");

            await ExpectAsync(client, HttpMethod.Get, "/api/test", "game-server", 200);
            await ExpectAsync(client, HttpMethod.Get, "/api/test", "game-server", 200);
            await ExpectAsync(client, HttpMethod.Get, "/api/test", "game-server", 200);
            await ExpectRateLimitedAsync(
                client,
                HttpMethod.Get,
                "/api/test",
                "game-server");
            Console.WriteLine("PASS player, administrator, and game-server budgets are independent.");

            await WaitForResetAsync();
            await ExpectAsync(client, HttpMethod.Get, "/api/test", "player-a", 200);
            Console.WriteLine("PASS principal limits replenish after the configured window.");

            await WaitForResetAsync();
            for (int attempt = 0; attempt < options.IngressPermitLimit; ++attempt)
            {
                await ExpectAsync(client, HttpMethod.Get, "/api/test", "invalid", 401);
            }
            await ExpectRateLimitedAsync(client, HttpMethod.Get, "/api/test", "invalid");
            await ExpectAsync(client, HttpMethod.Get, "/health", null, 200);
            await WaitForResetAsync();
            await ExpectAsync(client, HttpMethod.Get, "/api/test", "player-b", 200);
            Console.WriteLine("PASS invalid credentials are stopped by the pre-auth ingress budget.");
        }
        finally
        {
            await app.StopAsync();
        }

        Console.WriteLine("All API rate-limit tests passed.");
        return 0;
    }
    catch (Exception exception)
    {
        Console.Error.WriteLine($"FAIL {exception.GetType().Name}: {exception.Message}");
        return 1;
    }
}

static async Task FakeAuthenticationAsync(HttpContext context, RequestDelegate next)
{
    if (!context.Request.Path.StartsWithSegments("/api")
        || context.Request.Path.Equals(
            "/api/auth/steam-ticket",
            StringComparison.OrdinalIgnoreCase))
    {
        await next(context);
        return;
    }

    string authorization = context.Request.Headers.Authorization.ToString();
    AuthenticatedPrincipal? principal = authorization switch
    {
        "Bearer player-a" => new(PrincipalKind.Player, "steam-a", null),
        "Bearer player-b" => new(PrincipalKind.Player, "steam-b", null),
        "Bearer administrator" => new(PrincipalKind.Administrator, null, null),
        "Bearer game-server" => new(
            PrincipalKind.GameServer,
            null,
            "server-a",
            Guid.Parse("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")),
        _ => null
    };
    if (principal is null)
    {
        context.Response.StatusCode = StatusCodes.Status401Unauthorized;
        return;
    }
    context.Items[ApiAuthenticationMiddleware.PrincipalItemKey] = principal;
    await next(context);
}

static async Task ExpectRateLimitedAsync(
    HttpClient client,
    HttpMethod method,
    string path,
    string? token)
{
    using HttpResponseMessage response = await SendAsync(client, method, path, token);
    Require(response.StatusCode == HttpStatusCode.TooManyRequests,
        $"{path}: expected 429, got {(int)response.StatusCode}.");
    Require(response.Headers.RetryAfter?.Delta is not null,
        $"{path}: Retry-After was not returned.");
    Require(response.Headers.CacheControl?.NoStore == true,
        $"{path}: a rate-limit response may be cached.");
    using JsonDocument json = JsonDocument.Parse(
        await response.Content.ReadAsStringAsync());
    Require(json.RootElement.GetProperty("error").GetString()
            == "rate_limit_exceeded",
        $"{path}: unexpected rate-limit body.");
    Require(json.RootElement.EnumerateObject().Count() == 1,
        $"{path}: rate-limit response exposed unexpected details.");
}

static async Task ExpectAsync(
    HttpClient client,
    HttpMethod method,
    string path,
    string? token,
    int expectedStatus)
{
    using HttpResponseMessage response = await SendAsync(client, method, path, token);
    Require((int)response.StatusCode == expectedStatus,
        $"{path}: expected {expectedStatus}, got {(int)response.StatusCode}.");
}

static async Task<HttpResponseMessage> SendAsync(
    HttpClient client,
    HttpMethod method,
    string path,
    string? token)
{
    using HttpRequestMessage request = new(method, path);
    if (token is not null)
    {
        request.Headers.Authorization = new("Bearer", token);
    }
    return await client.SendAsync(request);
}

static Task WaitForResetAsync() => Task.Delay(TimeSpan.FromMilliseconds(1250));

static void Require(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}
