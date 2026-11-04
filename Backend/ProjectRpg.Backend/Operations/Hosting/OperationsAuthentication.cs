using System.Net.Http.Headers;
using System.Security.Claims;
using System.Text.Json;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using ProjectRpg.Backend.Operations.Infrastructure.Authentication;
using ProjectRpg.Backend.Operations.Web;

namespace ProjectRpg.Backend.Operations.Hosting;

public static class OperationsAuthentication
{
    public const string CookieScheme = "OperationsCookie";
    public const string GitHubScheme = "OperationsGitHub";
    public const string Policy = "OperationsAccess";

    public static void Register(IServiceCollection services, OperationsOptions settings)
    {
        services.AddAuthentication()
            .AddCookie(CookieScheme, options =>
            {
                options.Cookie.Name = "__Host-ProjectRpgOps";
                options.Cookie.HttpOnly = true;
                options.Cookie.SecurePolicy = CookieSecurePolicy.Always;
                options.Cookie.SameSite = SameSiteMode.Lax;
                options.Cookie.Path = "/";
                options.SlidingExpiration = false;
                options.ExpireTimeSpan = TimeSpan.FromHours(8);
                options.LoginPath = "/ops/login";
                options.Events.OnRedirectToLogin = context =>
                {
                    if (context.Request.Path.StartsWithSegments("/ops/api"))
                        return OperationsHttp.WriteErrorAsync(context.HttpContext, 401, "ops.authentication_required");
                    context.Response.Redirect("/ops/login");
                    return Task.CompletedTask;
                };
                options.Events.OnRedirectToAccessDenied = context =>
                    OperationsHttp.WriteErrorAsync(context.HttpContext, 403, "ops.access_denied");
                options.Events.OnValidatePrincipal = context =>
                {
                    if (context.Principal?.FindFirstValue(ClaimTypes.NameIdentifier) != settings.OperatorId)
                        context.RejectPrincipal();
                    return Task.CompletedTask;
                };
            })
            .AddOAuth(GitHubScheme, options =>
            {
                options.SignInScheme = CookieScheme;
                options.ClientId = settings.ClientId;
                options.ClientSecret = settings.ClientSecret;
                options.CallbackPath = "/ops/auth/github-callback";
                options.AuthorizationEndpoint = "https://github.com/login/oauth/authorize";
                options.TokenEndpoint = "https://github.com/login/oauth/access_token";
                options.UserInformationEndpoint = "https://api.github.com/user";
                options.UsePkce = true;
                options.SaveTokens = false;
                options.Scope.Clear();
                options.RemoteAuthenticationTimeout = TimeSpan.FromMinutes(10);
                options.BackchannelTimeout = TimeSpan.FromSeconds(10);
                options.CorrelationCookie.SecurePolicy = CookieSecurePolicy.Always;
                options.CorrelationCookie.HttpOnly = true;
                options.Events.OnCreatingTicket = async context =>
                {
                    using HttpRequestMessage request = new(HttpMethod.Get, options.UserInformationEndpoint);
                    request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", context.AccessToken);
                    request.Headers.UserAgent.ParseAdd("ProjectRpg-Operations/1.0");
                    request.Headers.Accept.ParseAdd("application/vnd.github+json");
                    using HttpResponseMessage response = await context.Backchannel.SendAsync(request, context.HttpContext.RequestAborted);
                    response.EnsureSuccessStatusCode();
                    using JsonDocument payload = JsonDocument.Parse(await response.Content.ReadAsStringAsync(context.HttpContext.RequestAborted));
                    if (!payload.RootElement.TryGetProperty("id", out JsonElement id) || !id.TryGetUInt64(out ulong numericId)
                        || numericId.ToString(System.Globalization.CultureInfo.InvariantCulture) != settings.AllowedGitHubUserId)
                        throw new OperatorDeniedException();
                    context.Principal = new ClaimsPrincipal(new ClaimsIdentity(
                        [new Claim(ClaimTypes.NameIdentifier, settings.OperatorId)], CookieScheme));
                    context.Properties.IssuedUtc = DateTimeOffset.UtcNow;
                    context.Properties.ExpiresUtc = DateTimeOffset.UtcNow.AddHours(8);
                    context.Properties.IsPersistent = false;
                };
                options.Events.OnRemoteFailure = async context =>
                {
                    context.HandleResponse();
                    bool unavailable = IsProviderFailure(context.Failure);
                    await OperationsHttp.WriteErrorAsync(context.HttpContext, unavailable ? 503 : 403,
                        unavailable ? "ops.auth_provider_unavailable" : "ops.access_denied");
                };
            });
        services.AddOptions<CookieAuthenticationOptions>(CookieScheme)
            .Configure<OperationsTicketStore>((options, store) => options.SessionStore = store);
        services.AddAuthorizationBuilder().AddPolicy(Policy, policy => policy.AddAuthenticationSchemes(CookieScheme)
            .RequireAuthenticatedUser().RequireClaim(ClaimTypes.NameIdentifier, settings.OperatorId));
        services.AddAntiforgery(options =>
        {
            options.Cookie.Name = "__Host-ProjectRpgOpsCsrf";
            options.Cookie.SecurePolicy = CookieSecurePolicy.Always;
            options.Cookie.HttpOnly = true;
            options.Cookie.Path = "/";
            options.HeaderName = "X-Ops-CSRF";
        });
    }
    private static bool IsProviderFailure(Exception? error)
    {
        for (; error is not null; error = error.InnerException)
            if (error is HttpRequestException or OperationCanceledException or JsonException) return true;
        return false;
    }
    private sealed class OperatorDeniedException : Exception;
}
