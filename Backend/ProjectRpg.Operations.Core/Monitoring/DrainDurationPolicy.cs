namespace ProjectRpg.Operations.Core.Monitoring;

/// <summary>Observed duration, independent of notice revisions and backend process lifetime.</summary>
internal static class DrainDurationPolicy
{
    internal static readonly TimeSpan Threshold = TimeSpan.FromMinutes(30);
    private static readonly TimeSpan MaximumSampleGap = TimeSpan.FromMinutes(2);

    internal static DateTimeOffset Observe(MonitorState state, DateTimeOffset now)
    {
        if (state.DrainObservedSinceUtc is null || state.DrainLastObservedAtUtc is not { } last
            || now < last || now - last > MaximumSampleGap)
            state.DrainObservedSinceUtc = now;
        state.DrainLastObservedAtUtc = now;
        return state.DrainObservedSinceUtc.Value;
    }

    internal static void Reset(MonitorState state)
    {
        state.DrainObservedSinceUtc = null;
        state.DrainLastObservedAtUtc = null;
    }
}
