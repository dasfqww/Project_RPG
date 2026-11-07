namespace ProjectRpg.Operations.Core.Monitoring;

/// <summary>Pure incident policy; transport/storage failures cannot become healthy samples.</summary>
public static class MonitorPolicy
{
    public static readonly IReadOnlySet<string> IncidentKeys = new HashSet<string>(StringComparer.Ordinal)
        { "backend_unavailable", "settlement_failed", "settlement_delayed", "log_dropped", "log_sink_error", "log_budget", "service_drain_long" };
    public static void Evaluate(MonitorState state, MonitorObservation observation, long logBudgetBytes)
    {
        var now = observation.ObservedAtUtc;
        var status = observation.Status;
        bool healthy = Valid(status, now) && status!.Ready;
        Update(state, "backend_unavailable", !healthy, 3, "Backend health, readiness or status collection failed.", now);
        if (!healthy)
        {
            DrainDurationPolicy.Reset(state);
            foreach(var pair in state.Incidents.Where(p=>p.Key!="backend_unavailable"))
            { pair.Value.BadSamples=0; pair.Value.GoodSamples=0; }
            return; // Unknown never resolves an incident using stale counters.
        }
        bool drain = status!.ServiceControl.Mode == "Drain";
        DateTimeOffset? drainSince = drain ? DrainDurationPolicy.Observe(state, now) : null;
        if (!drain) DrainDurationPolicy.Reset(state);
        Update(state, "service_drain_long", drain, 1, "Service has been observed in Drain for over thirty minutes. Investigate maintenance progress.", now,
            eligible: !drain || now - drainSince!.Value > DrainDurationPolicy.Threshold, badSince: drainSince);
        var settlements = status!.Settlements;
        state.Incidents.TryGetValue("settlement_failed",out var failed);
        bool newFailure=settlements.Failed>state.LastFailedCount || settlements.LatestFailedUpdatedAtUtc>state.LastFailedUpdatedAtUtc;
        Update(state, "settlement_failed", settlements.Failed > 0, 1, "Stored reward settlements require investigation.", now,
            newFailure ? settlements.Failed + ":" + settlements.LatestFailedUpdatedAtUtc?.ToString("O") : failed?.LastSignal);
        state.LastFailedCount=settlements.Failed; state.LastFailedUpdatedAtUtc=settlements.LatestFailedUpdatedAtUtc;
        Update(state, "settlement_delayed", settlements.OldestOutstandingAtUtc is { } oldest && now - oldest > TimeSpan.FromMinutes(2),
            3, "Reward settlement has remained pending or processing for over two minutes.", now);
        bool newBoot = state.BootId != status.BootId;
        long dropped = status.Logs.DroppedCount, errors = status.Logs.SinkErrorCount;
        Update(state, "log_dropped", dropped > (newBoot ? 0 : state.LastDropped), 1, "Operations log records were dropped.", now);
        Update(state, "log_sink_error", errors > (newBoot ? 0 : state.LastSinkErrors), 1, "Operations log storage reported an error.", now);
        Update(state, "log_budget", status.Logs.RetainedBytes > logBudgetBytes, 1, "Operations log storage exceeded its configured budget.", now);
        state.BootId = status.BootId; state.LastDropped = dropped; state.LastSinkErrors = errors;
    }
    public static bool Valid(RuntimeStatus? status, DateTimeOffset now) => status is not null && status.Settlements is not null && status.Logs is not null
        && status.ServiceControl is { Mode: "Open" or "Drain" }
        && status.ObservedAtUtc <= now.AddSeconds(30) && now - status.ObservedAtUtc <= TimeSpan.FromMinutes(2)
        && Guid.TryParseExact(status.BootId, "N", out var boot) && boot != Guid.Empty
        && status.Settlements.Pending >= 0 && status.Settlements.Processing >= 0 && status.Settlements.Failed >= 0
        && (status.Settlements.Pending == 0 && status.Settlements.Processing == 0 || status.Settlements.OldestOutstandingAtUtc is not null)
        && (status.Settlements.Failed == 0 || status.Settlements.LatestFailedUpdatedAtUtc is not null)
        && status.Logs.DroppedCount >= 0 && status.Logs.SinkErrorCount >= 0 && status.Logs.RetainedBytes >= 0
        && (status.Settlements.OldestOutstandingAtUtc is null || status.Settlements.OldestOutstandingAtUtc <= status.ObservedAtUtc)
        && (status.Settlements.LatestFailedUpdatedAtUtc is null || status.Settlements.LatestFailedUpdatedAtUtc <= status.ObservedAtUtc);
    private static void Update(MonitorState state, string key, bool bad, int threshold, string summary, DateTimeOffset now, string? signal = null,
        bool eligible = true, DateTimeOffset? badSince = null)
    {
        if (!state.Incidents.TryGetValue(key, out var incident)) state.Incidents.Add(key, incident = new());
        if (bad)
        {
            if (!incident.Active && (incident.BadSamples == 0 || badSince is not null)) incident.FirstSeenAtUtc = badSince ?? now;
            incident.BadSamples = Math.Min(threshold, incident.BadSamples + 1); incident.GoodSamples = 0;
            bool newlyFailed = key == "settlement_failed" && incident.Active && signal != incident.LastSignal;
            if (eligible && incident.BadSamples >= threshold && (!incident.Active || newlyFailed || now - incident.LastEventAtUtc >= TimeSpan.FromMinutes(30)))
            {
                incident.Active = true;
                Queue(state, key, "Firing", summary, incident, now);
            }
            incident.LastSignal = signal;
        }
        else
        {
            incident.BadSamples = 0; incident.GoodSamples = Math.Min(2, incident.GoodSamples + 1);
            if (incident.Active && incident.GoodSamples >= 2)
            {
                incident.Active = false; Queue(state, key, "Resolved", "The monitored condition recovered.", incident, now);
            }
        }
    }
    private static void Queue(MonitorState state, string key, string phase, string summary, IncidentState incident, DateTimeOffset now)
    {
        // One outstanding transition per incident. A recovery supersedes an undelivered firing event.
        state.Outbox.RemoveAll(p => p.Event.IncidentKey == key);
        state.Outbox.Add(new() { Event = new(Guid.NewGuid(), state.Environment, state.Service, key, phase == "Resolved" ? "Information" : "Warning",
            phase, incident.FirstSeenAtUtc, now, summary, []), NextAttemptAtUtc = now });
        incident.LastEventAtUtc = now;
    }
}

public sealed class MonitorCycle(IMonitorProbe probe, IMonitorStateStore storage, IAlertDelivery delivery,
    string environment, string service, long logBudgetBytes)
{
    public async Task ExecuteAsync(CancellationToken ct)
    {
        MonitorState state = await storage.LoadAsync(ct);
        if (state.Version != 1 || state.Environment != environment || state.Service != service)
            throw new InvalidOperationException("Monitor state belongs to another contract, environment or service.");
        var observation = await probe.ObserveAsync(ct);
        MonitorPolicy.Evaluate(state, observation, logBudgetBytes);
        await storage.SaveAsync(state, ct); // Persist event ID BEFORE delivery; retries/restarts reuse it.
        foreach (var pending in state.Outbox.Where(p => p.NextAttemptAtUtc <= observation.ObservedAtUtc).ToArray())
        {
            ct.ThrowIfCancellationRequested();
            bool sent = await delivery.SendAsync(pending.Event, ct);
            pending.Attempts++;
            if (sent || pending.Attempts >= 4) state.Outbox.Remove(pending); // Initial send + at most three retries.
            else pending.NextAttemptAtUtc = observation.ObservedAtUtc.AddMinutes(pending.Attempts switch { 1 => 1, 2 => 5, _ => 15 });
            await storage.SaveAsync(state, ct);
        }
    }
}
