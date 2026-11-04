using System.Text.Json;
using ProjectRpg.Operations.Core.Monitoring;
using ProjectRpg.Operations.Monitor;

internal static class MonitoringPolicyTests
{
    internal static RuntimeStatus Healthy(DateTimeOffset now) => new(now,"11111111111111111111111111111111",true,new(0,0,0,null,null),new(0,0,0,null));
    internal static async Task RunAsync()
    {
        var now=DateTimeOffset.UtcNow;
        var state=new MonitorState { Environment="Test",Service="backend" };
        void Evaluate(RuntimeStatus? status,int seconds=30) { now=now.AddSeconds(seconds); MonitorPolicy.Evaluate(state,new(now,status is null?null:status with { ObservedAtUtc=now }),1024); }
        Evaluate(null); Evaluate(null); Check.That(state.Outbox.Count==0,"Outage requires three consecutive failed observations.");
        Evaluate(null); Check.That(state.Outbox.Single().Event.IncidentKey=="backend_unavailable","Third failure alerts even when backend cannot send anything.");
        Guid outage=state.Outbox[0].Event.EventId;
        Evaluate(null); Check.That(state.Outbox[0].Event.EventId==outage,"Persistent outage does not flood alerts.");
        Evaluate(null,1800); Check.That(state.Outbox[0].Event.EventId!=outage,"Thirty-minute outage summary creates a new event.");
        Evaluate(Healthy(now)); Check.That(state.Incidents["backend_unavailable"].Active,"Single good sample does not resolve outage.");
        Evaluate(null); Evaluate(Healthy(now)); Evaluate(Healthy(now));
        Check.That(state.Outbox.Single(p=>p.Event.IncidentKey=="backend_unavailable").Event.State=="Resolved","Two consecutive good samples resolve outage.");
        state.Outbox.Clear();
        var failed=Healthy(now) with { Settlements=new(0,0,2,null,now.AddSeconds(-1)) };
        Evaluate(failed); Check.That(state.Outbox.Count==1,"Failed settlement present on first observation alerts immediately.");
        var first=state.Outbox[0].Event.EventId;
        Evaluate(failed with { Settlements=failed.Settlements with { Failed=1 } });
        Check.That(state.Outbox[0].Event.EventId==first,"A decreasing failed count is not a new failure.");
        Evaluate(failed with { Settlements=failed.Settlements with { Failed=1, LatestFailedUpdatedAtUtc=now } });
        Check.That(state.Outbox[0].Event.EventId!=first,"Advanced failed timestamp alerts even if total count does not increase.");
        state.Outbox.Clear();
        var delayed=Healthy(now) with { Settlements=new(1,1,0,now.AddMinutes(-3),null) };
        Evaluate(delayed); Evaluate(null); Evaluate(delayed); Evaluate(delayed);
        Check.That(!state.Incidents["settlement_delayed"].Active,"Unknown collection interrupts the delayed-settlement observation streak.");
        Evaluate(delayed); Check.That(state.Incidents["settlement_delayed"].Active,"Three over-two-minute observations alert.");
        Evaluate(Healthy(now)); Evaluate(null);
        Check.That(state.Incidents["settlement_delayed"].Active,"Collection failure never resolves stored settlement incidents.");
        Evaluate(Healthy(now)); Evaluate(Healthy(now));
        Check.That(!state.Incidents["settlement_delayed"].Active,"Two observed recoveries resolve settlement delay.");
        state.Outbox.Clear();
        Evaluate(Healthy(now) with { Logs=new(2,1,2048,now) });
        Check.That(new[]{"log_dropped","log_sink_error","log_budget"}.All(k=>state.Incidents[k].Active),"Log loss, sink failure and storage budget alert immediately.");
        Check.That(!MonitorPolicy.Valid(Healthy(now.AddMinutes(-3)),now),"Stale successful status cannot masquerade as current health.");
        Check.That(!MonitorPolicy.Valid(Healthy(now) with { Settlements=new(1,0,0,null,null) },now),"Incomplete delay evidence is unknown, not a healthy zero-delay sample.");
        bool incompleteRejected=false;
        try { JsonSerializer.Deserialize<RuntimeStatus>("{}",MonitoringJson.Options); } catch(JsonException) { incompleteRejected=true; }
        Check.That(incompleteRejected,"Missing status fields cannot default to zero and hide incidents.");
        await DurableRetries(now);
        Console.WriteLine("PASS Monitoring policy: failure/recovery hysteresis, reminders, failed-count/time edges, delay/unknown, logs and durable 1/5/15-minute retries.");
    }
    private static async Task DurableRetries(DateTimeOffset now)
    {
        string directory=Path.Combine(Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!,"monitor-state");
        var probe=new Probe { Now=now, FailedAt=now.AddDays(-1) }; var delivery=new Delivery();
        Guid eventId;
        using(var store=new FileMonitorStateStore(directory,"Test","backend"))
        {
            var cycle=new MonitorCycle(probe,store,delivery,"Test","backend",1024);
            await cycle.ExecuteAsync(default);
            eventId=delivery.Seen.Single().EventId;
            Check.That((await store.LoadAsync(default)).Outbox.Single().Attempts==1,"Initial failure is saved to the durable outbox.");
            bool locked=false;try { using var duplicate=new FileMonitorStateStore(directory,"Test","backend"); } catch(IOException){locked=true;}
            Check.That(locked,"Two monitor processes cannot race on a shared state directory.");
            probe.Now=now.AddSeconds(30); await cycle.ExecuteAsync(default);
            Check.That(delivery.Seen.Count==1,"Retry waits at least one minute.");
        }
        using(var restored=new FileMonitorStateStore(directory,"Test","backend"))
        {
            var cycle=new MonitorCycle(probe,restored,delivery,"Test","backend",1024);
            probe.Now=now.AddMinutes(1); await cycle.ExecuteAsync(default);
            probe.Now=now.AddMinutes(5); await cycle.ExecuteAsync(default); Check.That(delivery.Seen.Count==2,"Second backoff is five minutes.");
            probe.Now=now.AddMinutes(6); await cycle.ExecuteAsync(default);
            probe.Now=now.AddMinutes(21); await cycle.ExecuteAsync(default);
            Check.That(delivery.Seen.Count==4&&delivery.Seen.All(e=>e.EventId==eventId),"Restart reuses event ID through all three retries.");
            Check.That((await restored.LoadAsync(default)).Outbox.Count==0,"Retry attempts are bounded.");
        }
        string statePath=Path.Combine(directory,"state.json");
        await File.WriteAllTextAsync(statePath,"{corrupt}");
        using(var corrupt=new FileMonitorStateStore(directory,"Test","backend"))
        {
            bool rejected=false;try {await corrupt.LoadAsync(default);}catch(JsonException){rejected=true;}
            Check.That(rejected&&await File.ReadAllTextAsync(statePath)=="{corrupt}","Corrupt state fails closed and is not silently replaced.");
        }
    }
    private sealed class Probe : IMonitorProbe
    {
        public DateTimeOffset Now;
        public DateTimeOffset FailedAt;
        public Task<MonitorObservation> ObserveAsync(CancellationToken ct)=>Task.FromResult(new MonitorObservation(Now,Healthy(Now) with { Settlements=new(0,0,1,null,FailedAt) }));
    }
    private sealed class Delivery : IAlertDelivery
    {
        public List<AlertEvent> Seen=[];
        public Task<bool> SendAsync(AlertEvent alert,CancellationToken ct){Seen.Add(alert);return Task.FromResult(false);}
    }
}
