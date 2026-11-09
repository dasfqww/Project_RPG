using System.Diagnostics;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Operations.Hosting;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsGameStorageObserver : IGameStorageObserver, IDisposable
{
    public const string SourceName = "ProjectRpg.Game.Storage";
    private static readonly ActivitySource Source = new(SourceName);
    private readonly ActivityListener listener = new()
    {
        ShouldListenTo = source => source.Name == SourceName,
        Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded
    };
    private readonly IHttpContextAccessor context;
    private readonly OperationsEventRecorder events;

    public OperationsGameStorageObserver(IHttpContextAccessor context, OperationsEventRecorder events)
    {
        this.context = context;
        this.events = events;
        ActivitySource.AddActivityListener(listener);
    }

    public IGameStorageObservation? Begin(GameStorageOperation operation, Guid transactionRequestId)
    {
        if (context.HttpContext?.Items.TryGetValue(GameHttpDiagnostics.RequestIdItemKey, out object? value) != true
            || value is not Guid httpId || httpId == Guid.Empty || transactionRequestId == Guid.Empty
            || !Enum.IsDefined(operation)) return null;
        return new Observation(events, httpId, transactionRequestId, operation);
    }

    private sealed class Observation : IGameStorageObservation
    {
        private readonly OperationsEventRecorder events;
        private readonly Guid httpId, transactionRequestId;
        private readonly GameStorageOperation operation;
        private readonly Activity? activity;
        private readonly long started;
        private int ended;

        internal Observation(OperationsEventRecorder events, Guid httpId, Guid transactionRequestId, GameStorageOperation operation)
        {
            this.events = events; this.httpId = httpId; this.transactionRequestId = transactionRequestId; this.operation = operation;
            started = Stopwatch.GetTimestamp();
            // Without an actual HTTP Activity, retain HTTP-ID timing only; do not invent a new root trace.
            activity = Activity.Current is { TraceId: var traceId } && traceId != default
                ? Source.StartActivity("GameStorage." + operation, ActivityKind.Internal) : null;
        }

        public void End(GameStorageOutcome outcome)
        {
            if (Interlocked.Exchange(ref ended, 1) != 0) return;
            try
            {
                activity?.SetStatus(outcome == GameStorageOutcome.Returned ? ActivityStatusCode.Ok : ActivityStatusCode.Error);
                events.RecordGameStorage(httpId, transactionRequestId, operation, outcome,
                    Math.Clamp(Stopwatch.GetElapsedTime(started).TotalMilliseconds, 0, 86400000), activity);
            }
            finally { activity?.Dispose(); }
        }
    }

    public void Dispose() => listener.Dispose();
}
