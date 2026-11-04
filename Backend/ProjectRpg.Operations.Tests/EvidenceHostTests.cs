using System.Collections.Concurrent;
using System.Net;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Web;

internal static class EvidenceHostTests
{
    internal static async Task RunAsync(HostFixture host)
    {
        var services=host.App.Services;
        var game=services.GetRequiredService<IGameRepository>();
        var items=services.GetRequiredService<IItemRepository>();
        var economy=services.GetRequiredService<IEconomyRepository>();
        var character=(await game.GetCharacterEconomyContextAsync(host.CharacterId,default))!;
        var owner=new ItemOwnerRef(ItemOwnerType.Character,host.CharacterId.ToString("D"));
        DateTimeOffset now=host.Clock.Now;
        const string stranger="76561198012345679";
        await game.UpsertAccountAsync(new(stranger,stranger,false,false),now,default);
        var other=await game.CreateCharacterAsync(stranger,"Outside"+Guid.NewGuid().ToString("N")[..8],now,default);
        ItemRecord Item(ItemOwnerRef who) => new("OpsEvidence","Fixture",1,who,new(ItemContainerType.Mail,Guid.NewGuid().ToString("N"),0),
            new(Guid.NewGuid(),42,1,[],[]),0,ItemLifecycleState.Active,new(ItemBindState.Unbound,new(0,0),null,"Fixture",false));
        var grant=new ItemRepositoryCommitRequest(Guid.NewGuid(),"EvidenceGrant","fixture-grant",owner,1,[new(0,Item(owner))]);
        var granted=await items.CommitAsync(grant,now,default);
        Check.That(granted.Status==ItemRepositoryCommitStatus.Committed,"Evidence fixture grant commits.");
        var record=granted.Records[0];
        var consumed=record with {State=record.State with {Quantity=0},LifecycleState=ItemLifecycleState.Consumed,
            Location=new(ItemContainerType.Terminal,"",-1)};
        var consume=await items.CommitAsync(new(Guid.NewGuid(),"Consume","fixture-consume",owner,1,[new(record.Revision,consumed)]),now,default);
        Check.That(consume.Status==ItemRepositoryCommitStatus.Committed,"Evidence fixture consumes granted item.");
        var targetItem=Item(owner);
        var outsideItem=Item(new(ItemOwnerType.Character,other.CharacterId.ToString("D")));
        var system=await items.CommitAsync(new(Guid.NewGuid(),"SystemGrant","fixture-system",new(ItemOwnerType.System,"fixture"),2,
            [new(0,targetItem),new(0,outsideItem)]),now,default);
        Check.That(system.Status==ItemRepositoryCommitStatus.Committed,"Result-owner evidence fixture commits.");
        for(int i=0;i<4;i++)
            Check.That((await economy.CommitAsync(new(Guid.NewGuid(),host.CharacterId,host.SessionId,"EvidenceCoin","fixture-coin-"+i,"fixture",
                [new("OPS_COIN",1)]),character,now,default)).Status==CurrencyTransactionStatus.Committed,"Tied-time currency receipts commit.");

        async Task<Guid> Pending(bool invalidReward=false)
        {
            var session=await game.CreateDungeonSessionAsync(HostFixture.Account,host.CharacterId,"training","Normal",now,now.AddHours(1),default);
            await game.AssignDungeonServerAsync(session.DungeonSessionId,"ops-fixture","127.0.0.1:7777",now,now.AddHours(1),default);
            await game.StartDungeonSessionAsync(session.DungeonSessionId,"ops-fixture",now,now.AddHours(1),default);
            await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,"ops-fixture","ops_evidence_v1",new string('e',64),
                [new(invalidReward ? "OPS_MISSING_CURRENCY" : "OPS_COIN",7)], [new("OpsEvidence","Reward",1,1,ItemBindState.Unbound,0,0,[],[],1)],now,default);
            return session.DungeonSessionId;
        }
        Guid complete=await Pending();
        var observer=new ProbeObserver(services.GetRequiredService<IDungeonSettlementObserver>());
        using(var worker=new DungeonRewardSettlementWorker(game,services.GetRequiredService<IDungeonRewardCommitRepository>(),host.Clock,
            Options.Create(new DungeonSettlementOptions {PollIntervalMilliseconds=50}),NullLogger<DungeonRewardSettlementWorker>.Instance,observer))
        {
            await worker.StartAsync(default);
            try { await observer.Completed.Task.WaitAsync(TimeSpan.FromSeconds(10)); }
            finally { await worker.StopAsync(default); }
        }
        var commits=observer.Events.Where(e=>e.Phase==DungeonSettlementPhase.ReceiptCommitted).ToArray();
        Check.That(commits.Length==2 && commits.All(e=>e.SessionId==complete && e.CharacterId==host.CharacterId && e.TransactionRequestId.HasValue),
            "Real worker links committed currency/item request IDs even when observer throws.");
        Guid failed=await Pending(invalidReward:true);
        var failedObserver=new ProbeObserver(services.GetRequiredService<IDungeonSettlementObserver>());
        using(var worker=new DungeonRewardSettlementWorker(game,services.GetRequiredService<IDungeonRewardCommitRepository>(),host.Clock,
            Options.Create(new DungeonSettlementOptions {PollIntervalMilliseconds=50}),NullLogger<DungeonRewardSettlementWorker>.Instance,failedObserver))
        {
            await worker.StartAsync(default);
            try { await failedObserver.Completed.Task.WaitAsync(TimeSpan.FromSeconds(10)); }
            finally { await worker.StopAsync(default); }
        }
        Check.That(failedObserver.Events.Any(e=>e.Phase==DungeonSettlementPhase.Failed)
            &&failedObserver.Events.All(e=>e.Phase!=DungeonSettlementPhase.ReceiptCommitted),"Rejected atomic reward emits failure, never a committed receipt event.");
        Guid pending=await Pending();
        host.Clock.Now=now.AddSeconds(1);
        bool pg=services.GetService<NpgsqlDataSource>() is not null;
        string? before=pg ? await HostTests.FingerprintAsync(host) : null;
        string range="fromUtc="+Uri.EscapeDataString(now.AddDays(-1).UtcDateTime.ToString("O"))+"&toUtc="+Uri.EscapeDataString(now.AddMinutes(1).UtcDateTime.ToString("O"));
        string path=$"/ops/api/characters/{host.CharacterId}/timeline?{range}";
        var all=await Json(host,path+"&limit=200");
        var expected=all.GetProperty("items").EnumerateArray().Select(Key).ToArray();
        List<string> paged=[]; string? cursor=null;
        for(int page=0;page<30;page++)
        {
            var data=await Json(host,path+"&limit=2"+(cursor is null ? "" : "&cursor="+Uri.EscapeDataString(cursor)));
            paged.AddRange(data.GetProperty("items").EnumerateArray().Select(Key));
            cursor=data.GetProperty("nextCursor").GetString();
            if(cursor is null)break;
        }
        Check.That(paged.SequenceEqual(expected)&&paged.Distinct().Count()==paged.Count,"Merged tied-time source cursors have no loss/duplicate/reorder.");
        Check.That(expected.Count(x=>x=="ItemReceipt/"+grant.RequestId)==1,"Actor/result-owner match is deduplicated.");
        var original=await Json(host,$"/ops/api/characters/{host.CharacterId}/evidence/ItemReceipt/{grant.RequestId}");
        Check.That(original.GetProperty("status").GetString()=="Delivered"&&original.GetProperty("itemIds")[0].GetGuid()==record.ItemId,"Consumed item retains its immutable delivery receipt.");
        var linked=await Json(host,$"/ops/api/characters/{host.CharacterId}/evidence/ItemReceipt/{system.RequestId}");
        Check.That(linked.GetProperty("linkReason").GetString()=="OwnerScope"&&linked.GetProperty("itemIds").GetArrayLength()==1
            && linked.GetProperty("itemIds")[0].GetGuid()==targetItem.ItemId,"Owner-scope projection hides unrelated participant items.");
        Check.That((await host.Client.GetAsync($"/ops/api/characters/{other.CharacterId}/evidence/ItemReceipt/{grant.RequestId}")).StatusCode==HttpStatusCode.NotFound,"Exact evidence rechecks target association.");
        foreach(var pair in new[]{(complete,"Unknown"),(failed,"Failed"),(pending,"Pending")})
        {
            var row=await Json(host,$"/ops/api/characters/{host.CharacterId}/evidence/Settlement/{pair.Item1}");
            Check.That(row.GetProperty("status").GetString()==pair.Item2,"Settlement state does not replace receipt evidence.");
        }
        Check.That((await host.Client.GetAsync($"/ops/api/characters/{host.CharacterId}/timeline?fromUtc={now.AddDays(-31):yyyy-MM-dd}T00:00:00Z&toUtc={now:yyyy-MM-dd}T00:00:00Z")).StatusCode==HttpStatusCode.BadRequest,"Evidence time range is bounded.");
        string html=await host.Client.GetStringAsync($"/ops/characters/{host.CharacterId}/evidence?{range}");
        Check.That(html.Contains("보상 근거 조사")&&html.Contains(grant.RequestId.ToString())&&!html.Contains("private-fixture-error"),"Evidence Razor uses safe stored projections.");
        await Task.Delay(250);
        string logRange="fromUtc="+Uri.EscapeDataString(now.AddHours(-1).UtcDateTime.ToString("O"))+"&toUtc="+Uri.EscapeDataString(now.AddMinutes(1).UtcDateTime.ToString("O"));
        var logs=await Json(host,$"/ops/api/logs?transactionRequestId={commits[0].TransactionRequestId}&{logRange}");
        Check.That(logs.GetProperty("events").EnumerateArray().Any(e=>e.GetProperty("dungeonSessionId").GetGuid()==complete
            &&e.GetProperty("traceId").GetString()?.Length==32),"Persisted worker log joins original receipt to session/attempt trace.");
        if(pg) Check.That(before==await HostTests.FingerprintAsync(host),"Evidence queries leave all gameplay tables unchanged.");
        Console.WriteLine("PASS Evidence HTTP: tied-time merge, owner linkage/dedupe, consumed receipt, status separation, safe details, real worker and read-only gameplay.");
    }
    private static string Key(JsonElement row)=>row.GetProperty("sourceKind").GetString()+"/"+row.GetProperty("sourceKey").GetString();
    private static async Task<JsonElement> Json(HostFixture host,string path)
    {
        using var response=await host.Client.GetAsync(path);
        string body=await response.Content.ReadAsStringAsync();
        Check.That(response.IsSuccessStatusCode,"Evidence HTTP "+(int)response.StatusCode+" "+body);
        using var parsed=JsonDocument.Parse(body);
        return parsed.RootElement.GetProperty("data").Clone();
    }
    private sealed class ProbeObserver(IDungeonSettlementObserver inner):IDungeonSettlementObserver
    {
        internal ConcurrentQueue<DungeonSettlementObservation> Events=new();
        internal TaskCompletionSource Completed=new(TaskCreationOptions.RunContinuationsAsynchronously);
        public void Observe(DungeonSettlementObservation observation)
        {
            Events.Enqueue(observation); inner.Observe(observation);
            if(observation.Phase is DungeonSettlementPhase.Completed or DungeonSettlementPhase.Failed)Completed.TrySetResult();
            throw new InvalidOperationException("fixture-observer-failure");
        }
    }
}
