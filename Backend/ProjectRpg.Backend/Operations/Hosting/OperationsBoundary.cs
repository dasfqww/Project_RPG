using System.Diagnostics;
using System.Net;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Antiforgery;
using Npgsql;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Backend.Operations.Web;

namespace ProjectRpg.Backend.Operations.Hosting;

public sealed class OperationsRequestLimits : IDisposable
{
    private readonly PartitionedRateLimiter<string> limiter = PartitionedRateLimiter.Create<string, string>(key =>
        RateLimitPartition.GetFixedWindowLimiter(key, value => new FixedWindowRateLimiterOptions
        {
            PermitLimit = value.StartsWith("login:", StringComparison.Ordinal) ? 10 :
                value.StartsWith("callback:", StringComparison.Ordinal) ? 30 :
                value.StartsWith("logs:", StringComparison.Ordinal) ? 10 :
                value.StartsWith("commands:", StringComparison.Ordinal) ? 30 : 120,
            Window = value.StartsWith("login:", StringComparison.Ordinal) || value.StartsWith("callback:", StringComparison.Ordinal)
                ? TimeSpan.FromMinutes(10) : TimeSpan.FromMinutes(1),
            QueueLimit = 0, AutoReplenishment = true
        }));
    public RateLimitLease Acquire(string key) => limiter.AttemptAcquire(key);
    public void Dispose() => limiter.Dispose();
}

public sealed class OperationsBoundary(RequestDelegate next)
{
    public async Task InvokeAsync(HttpContext context, OperationsOptions options, OperationsRequestLimits limits,
        OperationsEventRecorder events)
    {
        if (!context.Request.Path.StartsWithSegments("/ops")) { await next(context); return; }
        using Activity requestActivity = new Activity("Operations.Http").SetIdFormat(ActivityIdFormat.W3C).Start();
        context.Items[OperationsHttp.RequestIdKey] = Guid.NewGuid();
        context.Response.Headers.CacheControl = "no-store";
        context.Response.Headers["X-Ops-Request-Id"] = OperationsHttp.RequestId(context).ToString("D");
        context.Response.Headers["X-Content-Type-Options"] = "nosniff";
        context.Response.Headers["Referrer-Policy"] = "no-referrer";
        string nonce = Convert.ToBase64String(System.Security.Cryptography.RandomNumberGenerator.GetBytes(24));
        context.Items["Ops.ScriptNonce"] = nonce;
        context.Response.Headers["Content-Security-Policy"] = $"default-src 'none'; script-src 'nonce-{nonce}'; connect-src 'self'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'";
        Stopwatch timer = Stopwatch.StartNew();
        events.Record(context, "ops.http.started", "OperationsRequest", "Started");
        CancellationToken original = context.RequestAborted;
        using CancellationTokenSource deadline = CancellationTokenSource.CreateLinkedTokenSource(original);
        bool login = context.Request.Path == "/ops/login" && HttpMethods.IsGet(context.Request.Method)
            || context.Request.Path == "/ops/auth/login" && HttpMethods.IsPost(context.Request.Method);
        bool callback = context.Request.Path == "/ops/auth/github-callback" && HttpMethods.IsGet(context.Request.Method);
        bool command = HttpMethods.IsPost(context.Request.Method) && context.Request.Path.StartsWithSegments("/ops/api");
        deadline.CancelAfter(TimeSpan.FromSeconds(login || callback ? 25 : command ? 10 : 5));
        context.RequestAborted = deadline.Token;
        try
        {
            IPAddress? address = context.Connection.RemoteIpAddress;
            if (address?.IsIPv4MappedToIPv6 == true) address = address.MapToIPv4();
            Uri expected = new(options.ManagementOrigin);
            if (!context.Request.IsHttps || !string.Equals(context.Request.Host.Value, expected.Authority, StringComparison.OrdinalIgnoreCase)
                || address is null || !options.AllowedNetworks.Any(n => System.Net.IPNetwork.Parse(n).Contains(address)))
            { await OperationsHttp.WriteErrorAsync(context, 403, "ops.access_denied"); return; }
            string partition;
            if (login || callback) partition = (callback ? "callback:" : "login:") + address;
            else
            {
                AuthenticateResult authenticated = await context.AuthenticateAsync(OperationsAuthentication.CookieScheme);
                if (!authenticated.Succeeded || authenticated.Principal is null)
                {
                    events.Record(context, "ops.access.denied", "OperationsAccess", "Denied", errorCode: "ops.authentication_required");
                    if (!context.Request.Path.StartsWithSegments("/ops/api") && HttpMethods.IsGet(context.Request.Method))
                        context.Response.Redirect("/ops/login");
                    else await OperationsHttp.WriteErrorAsync(context, 401, "ops.authentication_required");
                    return;
                }
                context.User = authenticated.Principal;
                partition = command ? "commands:" + options.OperatorId : (context.Request.Path.StartsWithSegments("/ops/api/logs") || context.Request.Path == "/ops/diagnostics"
                    ? "logs:" : "reads:") + options.OperatorId;
            }
            using RateLimitLease lease = limits.Acquire(partition);
            if (!lease.IsAcquired)
            {
                context.Response.Headers.RetryAfter = lease.TryGetMetadata(MetadataName.RetryAfter, out TimeSpan delay)
                    ? Math.Max(1, (int)Math.Ceiling(delay.TotalSeconds)).ToString(System.Globalization.CultureInfo.InvariantCulture) : "60";
                await OperationsHttp.WriteErrorAsync(context, 429, "ops.rate_limited");
                return;
            }
            if (command)
            {
                if (context.Request.ContentLength > 32 * 1024) { await OperationsHttp.WriteErrorAsync(context, 413, "ops.body_too_large"); return; }
                var size = context.Features.Get<Microsoft.AspNetCore.Http.Features.IHttpMaxRequestBodySizeFeature>();
                if (size is { IsReadOnly: false }) size.MaxRequestBodySize = 32 * 1024;
            }
            await next(context);
        }
        catch (AntiforgeryValidationException) { await OperationsHttp.WriteErrorAsync(context, 403, "ops.csrf_failed"); }
        catch (BadHttpRequestException e) { await OperationsHttp.WriteErrorAsync(context, e.StatusCode, e.StatusCode == 413 ? "ops.body_too_large" : "ops.invalid_query"); }
        catch (Exception e) when (e is NpgsqlException or TimeoutException)
        { await OperationsHttp.WriteErrorAsync(context, 503, "ops.storage_unavailable"); }
        catch (OperationCanceledException) when (!original.IsCancellationRequested)
        { await OperationsHttp.WriteErrorAsync(context, 503, "ops.storage_unavailable"); }
        catch (OperationCanceledException) when (original.IsCancellationRequested) { }
        catch (Exception) when (!context.Response.HasStarted)
        { await OperationsHttp.WriteErrorAsync(context, 500, "ops.internal_error"); }
        finally
        {
            context.RequestAborted = original;
            if (context.Items["Ops.QueryKind"] is string kind)
                events.Record(context, kind == "Evidence" ? (context.Response.StatusCode < 400 ? "ops.evidence.query.completed" : "ops.evidence.query.failed")
                    : kind == "Logs" ? (context.Response.StatusCode < 400 ? "ops.logs.search.completed" : "ops.logs.search.failed")
                    : (context.Response.StatusCode < 400 ? "ops.character.query.completed" : "ops.character.query.failed"),
                    kind, context.Response.StatusCode < 400 ? "Succeeded" : "Failed", timer.Elapsed.TotalMilliseconds,
                    context.Response.StatusCode >= 400 ? "ops.query_failed" : null,
                    context.Items["Ops.CharacterId"] as Guid?);
            events.Record(context, "ops.http.completed", "OperationsRequest",
                context.Response.StatusCode < 400 ? "Completed" : "Failed", timer.Elapsed.TotalMilliseconds,
                context.Response.StatusCode >= 400 ? "ops.request_failed" : null);
        }
    }
}
