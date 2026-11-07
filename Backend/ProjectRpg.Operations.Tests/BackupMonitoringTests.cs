using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using ProjectRpg.Operations.Core.Monitoring;
using ProjectRpg.Operations.Monitor;

internal static class BackupMonitoringTests
{
    private static BackupStatus Healthy(DateTimeOffset now) => new(1,"OpsTest","backend",now,true,true,false,false,
        now.AddDays(-1),now.AddHours(-1),2,true,now.AddDays(-2),null,0,null);
    internal static async Task RunAsync()
    {
        Policy();
        await TransportAsync();
        Console.WriteLine("PASS Backup monitoring: freshness/scope/strict file bounds, full/daily/retention/job/archive policy, idle WAL, independent backend failure, durable HTTPS retry/recovery and native collector schema.");
    }

    private static void Policy()
    {
        var now=DateTimeOffset.UtcNow; var state=new MonitorState { Environment="OpsTest",Service="backend" };
        void Sample(BackupStatus? value, bool backend=true) => MonitorPolicy.Evaluate(state,new(now,
            backend?MonitoringPolicyTests.Healthy(now):null,new(true,value is null?null:value with { ObservedAtUtc=now })),1024);
        Sample(Healthy(now)); Check.That(state.Outbox.Count==0,"Idle archive age alone is healthy without pending WAL or failures.");
        Sample(Healthy(now) with { LastFullAtUtc=now.AddDays(-8),LastIncrementalAtUtc=now.AddHours(-26) });
        Check.That(!state.Incidents["backup_overdue"].Active,"Exact age boundaries are not overdue.");
        Sample(Healthy(now) with { LastFullAtUtc=now.AddDays(-8).AddSeconds(-1) });
        Check.That(state.Incidents["backup_overdue"].Active,"Full age over eight days alerts.");
        Sample(Healthy(now) with { FullBackupFailed=true,FullChains=1,LastArchiveFailureAtUtc=now.AddSeconds(-1),LastArchivedAtUtc=null });
        Check.That(new[]{"backup_failed","backup_retention","backup_archive"}.All(k=>state.Incidents[k].Active),"Backup failure, insufficient chains and archive failure alert immediately.");
        Sample(null); Sample(Healthy(now),backend:false);
        Check.That(state.Incidents["backup_failed"].Active,"Unknown cannot count as a recovered backup sample.");
        Sample(Healthy(now),backend:false);
        Check.That(!state.Incidents["backup_failed"].Active&&!state.Incidents["backup_archive"].Active,"Backend outage does not block two good backup recoveries.");
        Sample(Healthy(now) with { PendingWalCount=1,OldestPendingWalAtUtc=now.AddMinutes(-2).AddSeconds(-1) });
        Check.That(state.Incidents["backup_archive"].Active,"Old pending WAL alerts even if a newer segment archived successfully.");
        Sample(Healthy(now) with { LastFullAtUtc=now,LastIncrementalAtUtc=null });
        Sample(Healthy(now) with { LastFullAtUtc=now,LastIncrementalAtUtc=null });
        Check.That(!state.Incidents["backup_overdue"].Active,"Fresh full success satisfies daily cadence even without an incremental yet.");
        Check.That(!BackupPolicy.Valid(Healthy(now.AddMinutes(-3)),now)
            &&!BackupPolicy.Valid(Healthy(now) with { PendingWalCount=1 },now)
            &&!BackupPolicy.Valid(Healthy(now) with { LastFullAtUtc=now.AddSeconds(1) },now),"Stale/inconsistent/future backup evidence is Unknown.");
        Sample(Healthy(now) with { Environment="Foreign" });
        Check.That(state.Incidents["backup_unavailable"].Active,"Foreign environment state cannot masquerade as this backup.");
        var disabled=new MonitorState(); MonitorPolicy.Evaluate(disabled,new(now,MonitoringPolicyTests.Healthy(now)),1024);
        Check.That(!disabled.Incidents.Keys.Any(BackupPolicy.IncidentKeys.Contains),"Unconfigured backup reader does not invent alarms or clear incidents.");
    }

    private static async Task TransportAsync()
    {
        string root=Path.Combine(Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!,"backup-monitor");
        Directory.CreateDirectory(root); string snapshot=Path.Combine(root,"backup-status.json");
        var now=DateTimeOffset.UtcNow; var status=Healthy(now) with { FullBackupFailed=true };
        await File.WriteAllTextAsync(snapshot,JsonSerializer.Serialize(status,MonitoringJson.Options));
        var reader=new FileBackupStatusReader(snapshot);
        Check.That((await reader.ReadAsync(default))==status,"Normalized backup JSON round-trips through the strict bounded reader.");
        var received=new List<AlertEvent>(); bool reject=true;
        await using var host=await HostFixture.StartAsync(Path.Combine(root,"host"),enabled:false,customizeRoutes:app=>
            app.MapPost("/test-backup-webhook",async(HttpContext c)=>
            {
                Check.That(c.Request.Headers.Authorization=="Bearer fixture-backup-webhook","Dedicated webhook credential only.");
                received.Add((await c.Request.ReadFromJsonAsync<AlertEvent>(MonitoringJson.Options))!);
                return Results.StatusCode(reject?503:204);
            }));
        var config=new MonitorConfiguration("OpsTest","backend",new(host.Origin),new('m',48),new(host.Origin+"/test-backup-webhook"),
            "fixture-backup-webhook",Path.Combine(root,"state"),1024*1024,snapshot);
        config.Validate(); var runtime=new RuntimeProbe { Now=now };
        var probe=new BackupAwareProbe(runtime,reader);var delivery=new HttpWebhookDelivery(host.Client,config);
        Guid id;
        using(var store=new FileMonitorStateStore(config.StateDirectory,config.Environment,config.Service))
        {
            await new MonitorCycle(probe,store,delivery,config.Environment,config.Service,config.LogBudgetBytes).ExecuteAsync(default);
            id=received.Single().EventId;
            Check.That((await store.LoadAsync(default)).Outbox.Single().Attempts==1,"Backup event is durably saved before failed HTTPS delivery.");
        }
        using(var restored=new FileMonitorStateStore(config.StateDirectory,config.Environment,config.Service))
        {
            var cycle=new MonitorCycle(probe,restored,delivery,config.Environment,config.Service,config.LogBudgetBytes);
            runtime.Now=now.AddMinutes(1);reject=false;await cycle.ExecuteAsync(default);
            Check.That(received.Count==2&&received[1].EventId==id,"Monitor restart retries the same backup event ID.");
            await File.WriteAllTextAsync(snapshot,"{broken}");
            runtime.Now=now.AddSeconds(90);await cycle.ExecuteAsync(default);
            Check.That((await restored.LoadAsync(default)).Incidents["backup_failed"].Active,"Corrupt file cannot resolve a previous backup failure.");
            for(int i=0;i<2;i++)
            {
                runtime.Now=runtime.Now.AddSeconds(30);await File.WriteAllTextAsync(snapshot,JsonSerializer.Serialize(Healthy(runtime.Now),MonitoringJson.Options));
                await cycle.ExecuteAsync(default);
            }
            Check.That(received.Last(e=>e.IncidentKey=="backup_failed").State=="Resolved","Two fresh backup observations deliver recovery.");
        }
        await File.WriteAllTextAsync(snapshot,new string('x',8193));
        Check.That(await reader.ReadAsync(default) is null,"Oversized backup file is unavailable.");
        await File.WriteAllTextAsync(snapshot,"{}");Check.That(await reader.ReadAsync(default) is null,"Incomplete fields are not default healthy values.");
        string? native=Environment.GetEnvironmentVariable("PROJECT_RPG_BACKUP_NATIVE_STATUS_DIRECTORY");
        if(native is not null)
        {
            var real=await new FileBackupStatusReader(Path.Combine(native,"healthy.json")).ReadAsync(default);
            Check.That(real is not null&&BackupPolicy.Valid(real,real.ObservedAtUtc)&&real.FullChains>=2&&real.LastIncrementalAtUtc is not null,
                "Actual Linux pgBackRest encrypted full/incr and read-only PostgreSQL collection match the monitor schema.");
            var failure=await new FileBackupStatusReader(Path.Combine(native,"failed.json")).ReadAsync(default);
            Check.That(failure is { FullBackupFailed:true,CollectionSucceeded:true },"Native failed backup receipt reaches the external monitor.");
            var archive=await new FileBackupStatusReader(Path.Combine(native,"archive-failed.json")).ReadAsync(default);
            Check.That(archive is { CollectionSucceeded:true,LastArchiveFailureAtUtc:not null,PendingWalCount:>0 },"Actual WAL failure and pending files reach the external monitor.");
            var nativeState=new MonitorState { Environment="OpsTest",Service="backend" };
            BackupPolicy.Evaluate(nativeState,new(true,archive),archive!.ObservedAtUtc);
            Check.That(nativeState.Incidents["backup_archive"].Active,"Actual PostgreSQL WAL failure fires the archive incident.");
            var unknown=await new FileBackupStatusReader(Path.Combine(native,"unknown.json")).ReadAsync(default);
            Check.That(unknown is { CollectionSucceeded:false },"Native collection failure is explicit Unknown.");
        }
    }

    private sealed class RuntimeProbe : IMonitorProbe
    {
        internal DateTimeOffset Now;
        public Task<MonitorObservation> ObserveAsync(CancellationToken ct)=>Task.FromResult(new MonitorObservation(Now,MonitoringPolicyTests.Healthy(Now)));
    }
}
