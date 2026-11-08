using System.Diagnostics;
using Microsoft.AspNetCore.Routing;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Operations.Hosting;

/// <summary>API attempt correlation, independent of authentication/business handlers and optional log I/O.</summary>
public sealed class GameHttpDiagnostics(RequestDelegate next)
{
    public const string RequestIdHeader = "X-Game-Request-Id";
    public const string TraceIdHeader = "X-Game-Trace-Id";
    public const string RequestIdItemKey = "ProjectRpg.GameHttpRequestId";

    public async Task InvokeAsync(HttpContext context, IGameHttpObserver observer)
    {
        if (!context.Request.Path.StartsWithSegments("/api")) { await next(context); return; }
        Guid id = Guid.NewGuid(); // Caller-supplied request numbers never become the internal attempt identity.
        context.Items[RequestIdItemKey] = id;
        var activity = Activity.Current;
        string? trace = activity is { TraceId: var traceId } && traceId != default ? traceId.ToString() : null;
        string? span = trace is not null && activity!.SpanId != default ? activity.SpanId.ToString() : null;
        string? parent = trace is not null && activity!.ParentSpanId != default ? activity.ParentSpanId.ToString() : null;
        string verb = context.Request.Method is "GET" or "POST" or "PUT" or "PATCH" or "DELETE" or "HEAD" or "OPTIONS"
            ? context.Request.Method : "OTHER";
        string? template = (context.GetEndpoint() as RouteEndpoint)?.RoutePattern.RawText;
        string operation = template is { Length: > 0 and <= 112 } ? verb + " " + template : verb + " GameApi.Unmatched";
        context.Response.OnStarting(() =>
        {
            context.Response.Headers[RequestIdHeader] = id.ToString("D");
            if (trace is not null) context.Response.Headers[TraceIdHeader] = trace;
            else context.Response.Headers.Remove(TraceIdHeader);
            return Task.CompletedTask;
        });
        long started = Stopwatch.GetTimestamp();
        void Record(GameHttpPhase phase, GameHttpOutcome outcome, int? code, double? elapsed)
        {
            try { observer.Observe(new(phase,id,trace,span,parent,operation,outcome,code,elapsed)); }
            catch (Exception) { /* Optional diagnostics cannot change an HTTP/business outcome. */ }
        }
        Record(GameHttpPhase.Started,GameHttpOutcome.Started,null,null);
        bool faulted = false, canceled = false;
        try { await next(context); }
        catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
        { canceled = true; throw; }
        catch { faulted = true; throw; }
        finally
        {
            canceled |= context.RequestAborted.IsCancellationRequested;
            int? code = canceled ? null : faulted ? context.Response.HasStarted ? null : 500 : context.Response.StatusCode;
            var outcome = canceled ? GameHttpOutcome.Canceled : faulted ? GameHttpOutcome.Faulted
                : code >= 400 ? GameHttpOutcome.Rejected : GameHttpOutcome.Succeeded;
            Record(GameHttpPhase.Completed,outcome,code,Math.Clamp(Stopwatch.GetElapsedTime(started).TotalMilliseconds,0,86400000));
        }
    }
}
