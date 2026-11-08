using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.WebUtilities;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class PickupDiagnosticsTests
{
    private const string Secret="private-pickup-76561198077777777@example.invalid";
    private const string Peer="76561198088889999";
    private const string Trace="4bf92f3577b34da6a3ce929d0e0e4736";
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        using var listener=new ActivityListener {ShouldListenTo=_=>true,
            Sample=(ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded};
        ActivitySource.AddActivityListener(listener);
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
            await Http(Path.Combine(root,pg is null?"pickup-memory":"pickup-postgres"),pg);
        await Http(Path.Combine(root,"pickup-broken-observer"),null,new Capture());
        await FailureBoundary();
        Console.WriteLine("PASS Pickup tracing: actual two-member HTTPS prepare/atomic consumption/replay, historical receipt after item movement, concurrent retries, session/character/item/request/trace search and Razor links, refusal/privacy/read-only boundaries and throwing-observer isolation.");
    }

    private static async Task Http(string root,string? pg,Capture? broken=null)
    {
        await using var host=await HostFixture.StartAsync(root,pg,gameDiagnostics:true,customize:services=>
        {
            services.AddSingleton<IDungeonPickupIntentRepository>(p=>(IDungeonPickupIntentRepository)p.GetRequiredService<IGameRepository>());
            services.AddSingleton<IDungeonPickupCommitRepository>(p=>(p.GetRequiredService<IGameRepository>(),p.GetRequiredService<IItemRepository>()) switch
            {
                (InMemoryGameRepository game,InMemoryItemRepository items)=>new InMemoryDungeonPickupCommitRepository(game,items,p.GetRequiredService<TimeProvider>()),
                (PostgresGameRepository game,PostgresItemRepository items)=>new PostgresDungeonPickupCommitRepository(game,items,p.GetRequiredService<TimeProvider>()),
                _=>throw new InvalidOperationException("Unsupported pickup test composition.")
            });
            services.AddSingleton<DungeonPickupCommitService>();
            if(broken is not null)services.AddSingleton<IGameTransactionObserver>(broken);
        },customizeRoutes:app=>app.MapDungeonPickupIntentApi());
        await host.SeedAsync();
        var game=host.App.Services.GetRequiredService<IGameRepository>();
        var items=host.App.Services.GetRequiredService<IItemRepository>();
        var intents=(IDungeonPickupIntentRepository)game;
        var now=host.Clock.Now;
        var a=await game.CreateCharacterAsync(HostFixture.Account,"PickA"+Guid.NewGuid().ToString("N")[..8],now,default);
        await game.UpsertAccountAsync(new(Peer,Peer,false,false),now,default);
        var b=await game.CreateCharacterAsync(Peer,"PickB"+Guid.NewGuid().ToString("N")[..8],now,default);
        var session=await game.CreateDungeonSessionAsync(HostFixture.Account,a.CharacterId,"training","Normal",now,now.AddHours(1),default);
        await game.JoinDungeonSessionAsync(session.DungeonSessionId,Peer,b.CharacterId,4,now,now.AddHours(1),default);
        const string server="pickup-tracing-fixture";
        await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",now,now.AddHours(1),default);
        await game.StartDungeonSessionAsync(session.DungeonSessionId,server,now,now.AddHours(1),default);
        var token=host.App.Services.GetRequiredService<AccessTokenService>().Issue(TimeSpan.FromHours(1));
        await game.StoreGameServerCredentialAsync(token.TokenHash,server,session.DungeonSessionId,token.ExpiresAt,default);
        var owner=new ItemOwnerRef(ItemOwnerType.Character,b.CharacterId.ToString("D"));
        var record=new ItemRecord("RPGItemDefinition","GameItem.Equipment.Weapon.GreatSword",1,owner,
            new(ItemContainerType.Inventory,owner.OwnerId,0),new(Guid.NewGuid(),7,1,[],[]),0,ItemLifecycleState.Active,
            new(ItemBindState.Unbound,new(0,0),null,"WorldPickup",false));
        var command=new ItemRepositoryCommitRequest(Guid.NewGuid(),"Item.Pickup",Secret,owner,1,[new(0,record)]);
        string route=$"/api/dungeon-sessions/{session.DungeonSessionId}/pickup-intents";
        string commit=route+$"/{command.RequestId}/commit";
        async Task<Guid> Send(string path,int expected,object? body=null,string? status=null,string? bearer=null)
        {
            using var request=new HttpRequestMessage(HttpMethod.Post,path);
            if(body is not null)request.Content=JsonContent.Create(body,body.GetType(),options:DungeonPickupIntentRules.JsonOptions);
            request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",bearer??token.Token);
            request.Headers.Add("traceparent","00-"+Trace+"-00f067aa0ba902b7-01");
            request.Headers.Add("X-Game-Request-Id",Secret);
            using var response=await host.Client.SendAsync(request);
            string text=await response.Content.ReadAsStringAsync();
            Check.That((int)response.StatusCode==expected,$"Pickup expected {expected}, got {(int)response.StatusCode}: {text}");
            if(status is not null){using var doc=JsonDocument.Parse(text);Check.That(doc.RootElement.GetProperty("status").GetString()==status,"Original pickup result remains unchanged.");}
            Check.That(response.Headers.GetValues("X-Game-Trace-Id").Single()==Trace,"Actual pickup attempt keeps its W3C trace.");
            return Guid.Parse(response.Headers.GetValues("X-Game-Request-Id").Single());
        }
        await Send(route+"/"+Guid.NewGuid()+"/commit",404);
        await Send(route,200,new PrepareDungeonPickupRequest(Guid.NewGuid(),DungeonPickupIntentRules.ToContract(command)),"Prepared");
        var prepared=(await intents.ReadPickupIntentAsync(session.DungeonSessionId,server,command.RequestId,now,default)).Intent!;
        Check.That(prepared.ConsumedAt is null&&await items.TryGetCommitResultAsync(command.RequestId,default) is null
            &&await items.FindAsync(record.ItemId,default) is null,"Reservation alone does not grant an item or produce a confirmed receipt.");
        await Send(commit,400,new { forged=Secret });
        await Send($"/api/dungeon-sessions/{Guid.NewGuid()}/pickup-intents/{command.RequestId}/commit",403);
        await Send(commit,403,bearer:"fixture-admin-token");
        await Send(commit,401,bearer:"invalid-pickup-token");
        Guid first=await Send(commit,200,status:"Committed");
        var original=(await items.TryGetCommitResultAsync(command.RequestId,default))!;
        string receipt=JsonSerializer.Serialize(original);
        var consumed=(await intents.ReadPickupIntentAsync(session.DungeonSessionId,server,command.RequestId,now,default)).Intent!;
        Check.That(consumed.ConsumedAt is not null&&consumed.CommandJson==prepared.CommandJson,"Actual commit consumes the frozen intent with its receipt.");
        // Replaying the historical receipt must never rewind the current item after a later movement.
        var current=(await items.FindAsync(record.ItemId,default))!;
        var moved=await items.CommitAsync(new(Guid.NewGuid(),"Item.Move",Secret,owner,1,
            [new(current.Revision,current with { Location=current.Location with { SlotIndex=1 } })]),now,default);
        Check.That(moved.Status==ItemRepositoryCommitStatus.Committed,"Later item movement committed.");
        string? fingerprint=pg is null?null:await HostTests.FingerprintAsync(host);
        Guid retry=await Send(commit,200,status:"AlreadyCommitted");
        Guid[] concurrent=await Task.WhenAll(Enumerable.Range(0,4).Select(_=>Send(commit,200,status:"AlreadyCommitted")));
        var httpIds=new[]{first,retry}.Concat(concurrent).ToHashSet();
        Check.That(httpIds.Count==6,"All exact/concurrent retries retain separate server HTTP identities.");
        Check.That(receipt==JsonSerializer.Serialize(await items.TryGetCommitResultAsync(command.RequestId,default))
            &&(await items.FindAsync(record.ItemId,default)) is {Revision:2,Location.SlotIndex:1},"Original receipt remains immutable and current item is not rewound or duplicated.");
        if(broken is null)
            Check.That((await host.App.Services.GetRequiredService<DungeonPickupCommitService>().CommitAsync(session.DungeonSessionId,server,command.RequestId,default)).Status
                ==DungeonPickupCommitStatus.AlreadyCommitted,"Direct background replay is supported without a fictitious HTTP scope.");
        using(var denied=await host.Client.GetAsync("/ops/api/logs?transactionRequestId="+command.RequestId))
            Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"Pickup IDs grant no operator access.");
        await host.LoginAsync();
        string Query(string field,string value)=>QueryHelpers.AddQueryString("/ops/api/logs",new Dictionary<string,string?>
        { [field]=value,["fromUtc"]=now.AddMinutes(-1).UtcDateTime.ToString("O"),["toUtc"]=now.AddMinutes(1).UtcDateTime.ToString("O") });
        if(broken is null)
        {
            JsonElement[] found=[];
            for(int attempt=0;attempt<30;attempt++)
            {
                using var response=await host.Client.GetAsync(Query("transactionRequestId",command.RequestId.ToString("D")));
                Check.That(response.IsSuccessStatusCode,"Protected pickup receipt search succeeds.");
                using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                found=doc.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Select(e=>e.Clone()).ToArray();
                if(found.Length==6)break;
                await Task.Delay(50);
            }
            Check.That(found.Length==6&&found.Select(e=>e.GetProperty("httpRequestId").GetGuid()).ToHashSet().SetEquals(httpIds)
                &&found.All(e=>e.GetProperty("traceId").GetString()==Trace&&e.GetProperty("operationName").GetString()=="DungeonPickup.Commit"
                    &&e.GetProperty("characterId").GetGuid()==b.CharacterId&&e.GetProperty("itemId").GetGuid()==record.ItemId
                    &&e.GetProperty("dungeonSessionId").GetGuid()==session.DungeonSessionId),"Actual stored receipt and verified scope connect all six HTTP attempts with the correct party member/item/session.");
            using var page=await host.Client.GetAsync(Query("transactionRequestId",command.RequestId.ToString("D")).Replace("/ops/api/logs","/ops/diagnostics",StringComparison.Ordinal));
            string html=await page.Content.ReadAsStringAsync();
            Check.That(page.IsSuccessStatusCode&&new[]{"httpRequestId","traceId","characterId","itemId","dungeonSessionId"}
                .All(field=>html.Contains("data-log-filter=\""+field+"\"")),"Real pickup records render their supported connection links without synthetic relationship rows.");
        }
        if(fingerprint is not null)Check.That(fingerprint==await HostTests.FingerprintAsync(host),"Replays and operator diagnostics leave gameplay state unchanged after the legitimate pickup/movement.");
        await host.StopAsync();
        string raw=string.Join('\n',Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").Select(File.ReadAllText));
        Check.That(!raw.Contains(Secret)&&!raw.Contains(token.Token)&&!raw.Contains(Peer)&&!raw.Contains(record.DefinitionName),"No frozen body/fingerprint, credential, account ID or item definition is logged.");
        var events=Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").SelectMany(File.ReadLines).Select(line=>
        {using var doc=JsonDocument.Parse(line);return doc.RootElement.GetProperty("Properties").GetProperty("OpsEvent").Deserialize<LogEvent>(OperationsHttp.Json)!;})
            .Where(e=>e.OperationName=="DungeonPickup.Commit").ToArray();
        if(broken is null)
            Check.That(events.Length==6&&events.Count(e=>e.Outcome=="Committed")==1&&events.Count(e=>e.Outcome=="Replayed")==5,
                "Only the actual commit and exact receipt replays are recorded, never reservation/refusal/background calls.");
        else
            Check.That(events.Length==0&&broken.Seen.Count==6&&broken.Seen.All(e=>e.Kind==GameTransactionKind.DungeonPickup
                &&e.CharacterId==b.CharacterId&&e.ItemId==record.ItemId&&e.DungeonSessionId==session.DungeonSessionId),
                "Every throwing observer sees only confirmed pickup facts while all HTTP results and single item effects are preserved.");
    }

    private static async Task FailureBoundary()
    {
        var capture=new Capture();var repository=new FailedRepository();
        var service=new DungeonPickupCommitService(repository,NullLogger<DungeonPickupCommitService>.Instance,capture);
        var result=await service.CommitAsync(Guid.NewGuid(),"server",Guid.NewGuid(),default);
        Check.That(result.Status==DungeonPickupCommitStatus.InternalError&&capture.Seen.Count==0,"Storage uncertainty produces no success observation and retains the existing safe failure result.");
        repository.Failure=new OperationCanceledException();bool canceled=false;
        try{await service.CommitAsync(Guid.NewGuid(),"server",Guid.NewGuid(),default);}catch(OperationCanceledException){canceled=true;}
        Check.That(canceled&&capture.Seen.Count==0,"Cancellation still propagates without a confirmed observation.");
    }
    private sealed class Capture : IGameTransactionObserver
    {
        internal System.Collections.Concurrent.ConcurrentBag<GameTransactionObservation> Seen=[];
        public void Observe(GameTransactionObservation observation){Seen.Add(observation);throw new IOException(Secret);}
    }
    private sealed class FailedRepository : IDungeonPickupCommitRepository
    {
        internal Exception Failure=new IOException(Secret);
        public Task<DungeonPickupCommitResult> CommitAsync(Guid session,string server,Guid request,CancellationToken ct)=>Task.FromException<DungeonPickupCommitResult>(Failure);
    }
}
