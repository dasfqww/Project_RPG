using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class StorageDiagnosticsTests
{
    private const string Secret = "storage-private-sql-password@example.invalid";

    internal static async Task RunAsync()
    {
        await Isolation();
        string root=Path.Combine(Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!,"storage-failures");
        var stopped=new ConcurrentDictionary<string,ActivityStatusCode>();
        // No sampling callback: the product adapter must create its own spans without an exporter.
        using var listener=new ActivityListener {ShouldListenTo=s=>s.Name==OperationsGameStorageObserver.SourceName,
            ActivityStopped=a=>stopped.TryAdd(a.SpanId.ToString(),a.Status)};
        ActivitySource.AddActivityListener(listener);
        await using var host=await HostFixture.StartAsync(root,gameDiagnostics:true,customizeRoutes:app=>
        {
            app.MapGet("/api/storage-probe/{mode}",async (HttpContext http,string mode)=>
            {
                var repository=new Repository(mode);
                var service=new ItemTransactionService(repository,TimeProvider.System,
                    storageObserver:http.RequestServices.GetRequiredService<IGameStorageObserver>());
                var parent=Activity.Current;
                try
                {
                    var result=await service.CommitAsync(Command(),default);
                    Check.That(mode=="returned"&&result.Status==ItemRepositoryCommitStatus.Committed,"Probe preserves repository result.");
                }
                catch(Exception error)
                {
                    Check.That(ReferenceEquals(error,repository.Error),"Original storage exception/cancellation propagates unchanged before test endpoint handles it.");
                }
                Check.That(ReferenceEquals(parent,Activity.Current),"Parent Activity is restored after returned/faulted/canceled storage calls.");
                return Results.Ok();
            });
        });
        Check.That(host.App.Services.GetRequiredService<IGameStorageObserver>().Begin(GameStorageOperation.ItemCommit,Guid.NewGuid()) is null,
            "Direct/background calls never invent an HTTP storage scope.");
        host.Client.DefaultRequestHeaders.Authorization=new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer","fixture-admin-token");
        var ids=new Dictionary<Guid,string>();
        Guid noTraceRequest;
        using(var response=await host.Client.GetAsync("/api/storage-probe/returned"))
        {
            Check.That(response.IsSuccessStatusCode&&!response.Headers.Contains(GameHttpDiagnostics.TraceIdHeader),"No forced HTTP listener means trace context is genuinely absent.");
            noTraceRequest=Guid.Parse(response.Headers.GetValues(GameHttpDiagnostics.RequestIdHeader).Single());
            ids.Add(noTraceRequest,"returned");
        }
        using var httpListener=new ActivityListener {ShouldListenTo=s=>s.Name=="Microsoft.AspNetCore",
            Sample=(ref ActivityCreationOptions<ActivityContext> _)=>ActivitySamplingResult.AllDataAndRecorded};
        ActivitySource.AddActivityListener(httpListener);
        foreach(string mode in new[]{"returned","read-failed","read-canceled","commit-failed","commit-canceled"})
        {
            using var response=await host.Client.GetAsync("/api/storage-probe/"+mode);
            Check.That(response.IsSuccessStatusCode,"Only the fixture handles the original injected storage failure.");
            ids.Add(Guid.Parse(response.Headers.GetValues(GameHttpDiagnostics.RequestIdHeader).Single()),mode);
        }
        // Two simultaneous requests must retain different HTTP, transaction and child span identities.
        await Task.WhenAll(Enumerable.Range(0,2).Select(async _=>
        {
            using var response=await host.Client.GetAsync("/api/storage-probe/returned");
            Check.That(response.IsSuccessStatusCode,"Concurrent storage call completed.");
            lock(ids)ids.Add(Guid.Parse(response.Headers.GetValues(GameHttpDiagnostics.RequestIdHeader).Single()),"returned");
        }));
        host.Client.DefaultRequestHeaders.Authorization=null;
        using(var denied=await host.Client.GetAsync("/ops/api/logs?eventName=game.storage.failed"))
            Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"Storage diagnostics retains operator authorization.");
        await host.LoginAsync();
        foreach(var (id,mode) in ids)
        {
            int expected=mode.StartsWith("read-",StringComparison.Ordinal)?3:4;
            JsonElement[] events=[];
            string from=Uri.EscapeDataString(host.Clock.Now.AddMinutes(-1).UtcDateTime.ToString("O"));
            string to=Uri.EscapeDataString(host.Clock.Now.AddMinutes(1).UtcDateTime.ToString("O"));
            for(int retry=0;retry<40;retry++)
            {
                using var response=await host.Client.GetAsync($"/ops/api/logs?httpRequestId={id}&fromUtc={from}&toUtc={to}");
                Check.That(response.IsSuccessStatusCode,$"Persisted storage timeline is queryable: {(int)response.StatusCode} {await response.Content.ReadAsStringAsync()}");
                using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                events=doc.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Select(e=>e.Clone()).ToArray();
                if(events.Length==expected)break;
                await Task.Delay(50);
            }
            Check.That(events.Length==expected,"Each call records one terminal storage observation, without fabricated receipt events.");
            var http=events.Single(e=>e.GetProperty("eventName").GetString()=="game.http.started");
            var calls=events.Where(e=>e.GetProperty("eventName").GetString()!.StartsWith("game.storage.",StringComparison.Ordinal)).ToArray();
            Check.That(calls.Select(e=>e.GetProperty("transactionRequestId").GetGuid()).Distinct().Count()==1,"All calls in one command retain the same requested transaction ID.");
            foreach(var call in calls)
            {
                bool failed=mode.EndsWith("failed",StringComparison.Ordinal)&&call.GetProperty("operationName").GetString()==(mode.StartsWith("read-",StringComparison.Ordinal)?"GameStorage.ItemReceiptRead":"GameStorage.ItemCommit");
                bool canceled=mode.EndsWith("canceled",StringComparison.Ordinal)&&call.GetProperty("operationName").GetString()==(mode.StartsWith("read-",StringComparison.Ordinal)?"GameStorage.ItemReceiptRead":"GameStorage.ItemCommit");
                string eventName=failed?"game.storage.failed":canceled?"game.storage.canceled":"game.storage.returned";
                Check.That(call.GetProperty("eventName").GetString()==eventName&&call.GetProperty("errorCode").GetString()==(failed||canceled?eventName:null),"Only fixed terminal/error codes cross the observation boundary.");
                if(id==noTraceRequest)
                {
                    Check.That(call.GetProperty("traceId").ValueKind==JsonValueKind.Null&&call.GetProperty("spanId").ValueKind==JsonValueKind.Null
                        &&call.GetProperty("parentSpanId").ValueKind==JsonValueKind.Null&&call.GetProperty("durationMs").GetDouble()>=0,
                        "Missing parent context retains timing and HTTP ID without inventing a root trace or a child relationship.");
                    continue;
                }
                Check.That(call.GetProperty("traceId").GetString()==http.GetProperty("traceId").GetString()
                    &&call.GetProperty("parentSpanId").GetString()==http.GetProperty("spanId").GetString()
                    &&call.GetProperty("durationMs").GetDouble()>=0,"Real child span/time belongs to the correct concurrent HTTP parent.");
                Check.That(stopped.TryGetValue(call.GetProperty("spanId").GetString()!,out var status)
                    &&status==(failed||canceled?ActivityStatusCode.Error:ActivityStatusCode.Ok),"Actual Activity was ended with the correct status before response.");
            }
        }
        await host.StopAsync();
        string raw=string.Join('\n',Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").Select(File.ReadAllText));
        Check.That(!raw.Contains(Secret),"Repository exception text and command body never enter storage diagnostics.");
        Console.WriteLine("PASS Storage diagnostics: original result/exception/cancellation isolation, begin/end observer failures, actual sibling Activity lifecycle without exporter, concurrent HTTPS correlation, fixed error privacy and protected JSONL search. Storage failure fixtures do not claim database rollback.");
    }

    private static async Task Isolation()
    {
        foreach(string observation in new[]{"none","normal","begin-throws","end-throws"})
        foreach(string mode in new[]{"returned","read-failed","read-canceled","commit-failed","commit-canceled"})
        {
            var capture=new Capture(observation);
            var repository=new Repository(mode);
            var service=new ItemTransactionService(repository,TimeProvider.System,storageObserver:observation=="none"?null:capture);
            Exception? error=null;
            try {var result=await service.CommitAsync(Command(),default);Check.That(mode=="returned"&&ReferenceEquals(result,repository.Result),"Observation returns the exact original committed result.");}
            catch(Exception value){error=value;}
            Check.That(mode=="returned"?error is null:ReferenceEquals(error,repository.Error),"Throwing/null observers cannot replace original errors or cancellation.");
            Check.That(repository.Reads==1&&repository.Commits==(mode.StartsWith("read-",StringComparison.Ordinal)?0:1),"Repository calls execute exactly once, even if diagnostics fail.");
            if(observation is "normal" or "end-throws")
                Check.That(capture.Outcomes.Count==repository.Reads+repository.Commits&&capture.Outcomes.Last()==(mode=="returned"?GameStorageOutcome.Returned:mode.EndsWith("canceled",StringComparison.Ordinal)?GameStorageOutcome.Canceled:GameStorageOutcome.Faulted),"Every begun observation ends with the actual terminal outcome.");
        }
    }

    private static ItemRepositoryCommitRequest Command()
    {
        var owner=new ItemOwnerRef(ItemOwnerType.Character,Guid.NewGuid().ToString("D"));
        var record=new ItemRecord("RPGItemDefinition","Potion",1,owner,new(ItemContainerType.Inventory,owner.OwnerId,0),
            new(Guid.NewGuid(),7,7,[],[]),0,ItemLifecycleState.Active,new(ItemBindState.Unbound,new(5,10),null,Secret,false));
        var command=new ItemRepositoryCommitRequest(Guid.NewGuid(),"StoragePrivateOperation",Secret,owner,7,[new(0,record)]);
        Check.That(ItemRecordRules.TryValidateCommit(command,out _),"Storage fixture uses a valid business command.");
        return command;
    }
    private sealed class Capture(string mode) : IGameStorageObserver, IGameStorageObservation
    {
        internal List<GameStorageOutcome> Outcomes=[];
        public IGameStorageObservation Begin(GameStorageOperation operation,Guid id)
        { if(mode=="begin-throws")throw new IOException(Secret);return this; }
        public void End(GameStorageOutcome outcome){Outcomes.Add(outcome);if(mode=="end-throws")throw new IOException(Secret);}
    }
    private sealed class Repository(string mode) : IItemRepository
    {
        internal Exception Error=mode.EndsWith("canceled",StringComparison.Ordinal)?new OperationCanceledException(Secret):new IOException(Secret);
        internal int Reads,Commits;
        internal ItemRepositoryCommitResult? Result;
        public async Task<ItemRepositoryCommitResult?> TryGetCommitResultAsync(Guid id,CancellationToken ct)
        { Reads++;await Task.Yield();if(mode.StartsWith("read-",StringComparison.Ordinal))throw Error;return null; }
        public async Task<ItemRepositoryCommitResult> CommitAsync(ItemRepositoryCommitRequest request,DateTimeOffset now,CancellationToken ct)
        {
            Commits++;await Task.Yield();if(mode.StartsWith("commit-",StringComparison.Ordinal))throw Error;
            return Result=new(ItemRepositoryCommitStatus.Committed,request.RequestId,request.Operation,request.CommandFingerprint,request.Actor,request.AffectedQuantity,
                request.Mutations.Select(m=>m.NewRecord with {Revision=1}).ToArray(),now);
        }
        public Task<ItemRecord?> FindAsync(Guid id,CancellationToken ct)=>throw new NotSupportedException();
        public Task<IReadOnlyList<ItemRecord>> FindByOwnerAsync(ItemOwnerRef owner,bool terminal,int limit,CancellationToken ct)=>throw new NotSupportedException();
    }
}
