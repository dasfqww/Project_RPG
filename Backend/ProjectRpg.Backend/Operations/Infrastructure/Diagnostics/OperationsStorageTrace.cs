using System.Diagnostics;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsStorageTrace : IDisposable
{
    private static readonly ActivitySource Source = new("ProjectRpg.Operations.Storage");
    private readonly ActivityListener listener = new()
    {
        ShouldListenTo = source => source.Name == "ProjectRpg.Operations.Storage",
        Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded
    };
    private readonly IHttpContextAccessor context;
    private readonly OperationsEventRecorder events;
    public OperationsStorageTrace(IHttpContextAccessor context, OperationsEventRecorder events)
    {
        this.context = context; this.events = events;
        ActivitySource.AddActivityListener(listener);
    }
    internal async Task<Result<T>> ObserveAsync<T>(Func<Task<Result<T>>> read) where T : notnull
    {
        using Activity? activity = Source.StartActivity("ops.storage.query");
        Stopwatch elapsed = Stopwatch.StartNew();
        bool success = false;
        try { var result = await read(); success = result.IsSuccess; return result; }
        finally
        {
            activity?.SetStatus(success ? ActivityStatusCode.Ok : ActivityStatusCode.Error);
            if (context.HttpContext is { } request)
                events.Record(request, success ? "ops.storage.query.completed" : "ops.storage.query.failed", "ReadStoredSnapshot",
                    success ? "Succeeded" : "Failed", elapsed.Elapsed.TotalMilliseconds,
                    success ? null : "ops.storage_query_failed");
        }
    }
    public void Dispose() => listener.Dispose();
}
