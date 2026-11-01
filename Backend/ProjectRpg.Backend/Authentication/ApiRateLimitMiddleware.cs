using System.Globalization;
using System.Threading.RateLimiting;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Authentication;

public sealed class ApiIngressRateLimitMiddleware(RequestDelegate next)
{
    public async Task InvokeAsync(
        HttpContext context,
        ApiRateLimiter rateLimiter)
    {
        if (!rateLimiter.Enabled
            || !context.Request.Path.StartsWithSegments("/api"))
        {
            await next(context);
            return;
        }

        using RateLimitLease lease = await rateLimiter.AcquireIngressAsync(
            context,
            context.RequestAborted);
        if (!lease.IsAcquired)
        {
            await WriteRejectedAsync(context, lease);
            return;
        }
        await next(context);
    }

    internal static async Task WriteRejectedAsync(
        HttpContext context,
        RateLimitLease lease)
    {
        context.Response.StatusCode = StatusCodes.Status429TooManyRequests;
        context.Response.Headers.CacheControl = "no-store";
        if (lease.TryGetMetadata(
                MetadataName.RetryAfter,
                out TimeSpan retryAfter))
        {
            int retryAfterSeconds = Math.Max(
                1,
                (int)Math.Ceiling(retryAfter.TotalSeconds));
            context.Response.Headers.RetryAfter =
                retryAfterSeconds.ToString(CultureInfo.InvariantCulture);
        }
        await context.Response.WriteAsJsonAsync(
            new { error = "rate_limit_exceeded" },
            context.RequestAborted);
    }
}

public sealed class AuthenticatedApiRateLimitMiddleware(RequestDelegate next)
{
    public async Task InvokeAsync(
        HttpContext context,
        ApiRateLimiter rateLimiter)
    {
        if (!rateLimiter.Enabled
            || !context.Request.Path.StartsWithSegments("/api")
            || !context.Items.TryGetValue(
                ApiAuthenticationMiddleware.PrincipalItemKey,
                out object? value)
            || value is not AuthenticatedPrincipal principal)
        {
            await next(context);
            return;
        }

        using RateLimitLease lease = await rateLimiter.AcquirePrincipalAsync(
            principal,
            context.RequestAborted);
        if (!lease.IsAcquired)
        {
            await ApiIngressRateLimitMiddleware.WriteRejectedAsync(context, lease);
            return;
        }
        await next(context);
    }
}
