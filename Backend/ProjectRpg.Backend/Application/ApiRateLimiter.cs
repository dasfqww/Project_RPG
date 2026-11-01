using System.Net;
using System.Threading.RateLimiting;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Application;

public sealed class ApiRateLimiter : IAsyncDisposable
{
    private const string AuthenticationPath = "/api/auth/steam-ticket";

    private readonly ApiRateLimitOptions _options;
    private readonly TimeSpan _window;
    private readonly PartitionedRateLimiter<HttpContext> _ingressLimiter;
    private readonly PartitionedRateLimiter<AuthenticatedPrincipal> _principalLimiter;

    public ApiRateLimiter(IOptions<ApiRateLimitOptions> options)
    {
        _options = options.Value;
        _window = TimeSpan.FromSeconds(_options.WindowSeconds);
        _ingressLimiter = PartitionedRateLimiter.Create<HttpContext, string>(
            CreateIngressPartition);
        _principalLimiter =
            PartitionedRateLimiter.Create<AuthenticatedPrincipal, string>(
                CreatePrincipalPartition);
    }

    public bool Enabled => _options.Enabled;

    public ValueTask<RateLimitLease> AcquireIngressAsync(
        HttpContext context,
        CancellationToken cancellationToken)
    {
        return _ingressLimiter.AcquireAsync(
            context,
            permitCount: 1,
            cancellationToken);
    }

    public ValueTask<RateLimitLease> AcquirePrincipalAsync(
        AuthenticatedPrincipal principal,
        CancellationToken cancellationToken)
    {
        return _principalLimiter.AcquireAsync(
            principal,
            permitCount: 1,
            cancellationToken);
    }

    private RateLimitPartition<string> CreateIngressPartition(HttpContext context)
    {
        bool isAuthentication = context.Request.Path.Equals(
            AuthenticationPath,
            StringComparison.OrdinalIgnoreCase);
        string scope = isAuthentication ? "authentication" : "api";
        string address = NormalizeAddress(context.Connection.RemoteIpAddress);
        int permitLimit = isAuthentication
            ? _options.AuthenticationPermitLimit
            : _options.IngressPermitLimit;
        return CreateFixedWindowPartition(
            $"{scope}:{address}",
            permitLimit);
    }

    private RateLimitPartition<string> CreatePrincipalPartition(
        AuthenticatedPrincipal principal)
    {
        (string key, int permitLimit) = principal.Kind switch
        {
            PrincipalKind.Player => (
                $"player:{principal.SteamId ?? "unknown"}",
                _options.PlayerPermitLimit),
            PrincipalKind.GameServer => (
                $"game-server:{principal.ServerId ?? "unknown"}:"
                    + $"{principal.DungeonSessionId?.ToString("N") ?? "unknown"}",
                _options.GameServerPermitLimit),
            PrincipalKind.Administrator => (
                "administrator",
                _options.AdministratorPermitLimit),
            _ => ("unknown", _options.PlayerPermitLimit)
        };
        return CreateFixedWindowPartition(key, permitLimit);
    }

    private RateLimitPartition<string> CreateFixedWindowPartition(
        string key,
        int permitLimit)
    {
        return RateLimitPartition.GetFixedWindowLimiter(
            key,
            _ => new FixedWindowRateLimiterOptions
            {
                AutoReplenishment = true,
                PermitLimit = permitLimit,
                QueueLimit = 0,
                QueueProcessingOrder = QueueProcessingOrder.OldestFirst,
                Window = _window
            });
    }

    private static string NormalizeAddress(IPAddress? address)
    {
        if (address is null)
        {
            return "unknown";
        }
        return address.IsIPv4MappedToIPv6
            ? address.MapToIPv4().ToString()
            : address.ToString();
    }

    public async ValueTask DisposeAsync()
    {
        await _ingressLimiter.DisposeAsync();
        await _principalLimiter.DisposeAsync();
    }
}
