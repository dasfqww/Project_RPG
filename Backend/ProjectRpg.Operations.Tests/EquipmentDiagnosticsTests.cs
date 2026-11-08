using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.WebUtilities;
using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class EquipmentDiagnosticsTests
{
    private const string Secret="private-equipment-76561198077777777@example.invalid";
    private const string Trace="4bf92f3577b34da6a3ce929d0e0e4736";
    private const string Target="GameItem.Equipment.Progression.Target.EnduranceGreatSword";
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        using var listener=new ActivityListener {ShouldListenTo=_=>true,
            Sample=(ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded};
        ActivitySource.AddActivityListener(listener);
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
            await Http(Path.Combine(root,pg is null?"equipment-memory":"equipment-postgres"),pg);
        await Http(Path.Combine(root,"equipment-broken-observer"),null,broken:new Capture());
        await Http(Path.Combine(root,"equipment-diagnostics-off"),null,enabled:false);
        await FilterBoundary();
        Console.WriteLine("PASS Equipment tracing: real HTTPS craft/upgrade/transfer/dismantle and historical replays, protected receipt/HTTP/trace search, refusals/privacy/unchanged replay state, throwing observer and diagnostics-off compatibility.");
    }

    private static async Task Http(string root,string? pg,bool enabled=true,Capture? broken=null)
    {
        await using var host=await HostFixture.StartAsync(root,pg,gameDiagnostics:enabled,customize:services=>
        {
            services.AddSingleton<IEquipmentProgressionRepository>(p=>(IEquipmentProgressionRepository)p.GetRequiredService<IGameRepository>());
            if(broken is not null)services.AddSingleton<IGameTransactionObserver>(broken);
        },customizeRoutes:app=>app.MapEquipmentProgressionApi());
        await host.SeedAsync();
        var game=host.App.Services.GetRequiredService<IGameRepository>();
        var items=host.App.Services.GetRequiredService<IItemRepository>();
        var now=host.Clock.Now;
        var character=await game.CreateCharacterAsync(HostFixture.Account,"Equip"+Guid.NewGuid().ToString("N")[..8],now,default);
        const string peer="76561198088887777";
        await game.UpsertAccountAsync(new(peer,peer,false,false),now,default);
        var foreign=await game.CreateCharacterAsync(peer,"Other"+Guid.NewGuid().ToString("N")[..8],now,default);
        var token=host.App.Services.GetRequiredService<AccessTokenService>().Issue(TimeSpan.FromHours(1));
        await game.StoreSessionAsync(token.TokenHash,HostFixture.Account,token.ExpiresAt,default);
        var owner=new ItemOwnerRef(ItemOwnerType.Character,character.CharacterId.ToString("D"));
        ItemRecord Make(string name,int slot,int quantity,bool gear=false)=>new("RPGItemDefinition",name,1,owner,
            new(ItemContainerType.Inventory,owner.OwnerId,slot),new(Guid.NewGuid(),0,quantity,[],[],gear?FirstSliceEquipmentProgression.Create():null),
            0,ItemLifecycleState.Active,new(ItemBindState.CharacterBound,new(0,0),null,"EquipmentFixture",false));
        var ore=Make(FirstSliceEquipmentDefinitions.OreDefinitionName,0,99);
        var tokens=Make(FirstSliceEquipmentDefinitions.TokenDefinitionName,1,16);
        var starter=Make(FirstSliceEquipmentDefinitions.All[0].DefinitionName,2,1,true);
        Check.That((await items.CommitAsync(new(Guid.NewGuid(),"EquipmentFixture",Secret,owner,0,
            [new(0,ore),new(0,tokens),new(0,starter)]),now,default)).Status==ItemRepositoryCommitStatus.Committed,"Owned equipment inputs seeded.");
        string route=$"/api/characters/{character.CharacterId}/equipment/";
        var commands=new List<(string Action,object Body,Guid Request,Guid? Http)>();
        async Task<(Guid? Http,JsonElement Body)> Send(string action,object body,int code=200,string? disposition=null,string? bearer=null,string? customRoute=null)
        {
            using var request=new HttpRequestMessage(HttpMethod.Post,(customRoute??route)+action)
                {Content=JsonContent.Create(body,body.GetType(),options:OperationsHttp.Json)};
            request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",bearer??token.Token);
            request.Headers.Add("traceparent","00-"+Trace+"-00f067aa0ba902b7-01");request.Headers.Add("X-Private-Input",Secret);
            using var response=await host.Client.SendAsync(request);
            string text=await response.Content.ReadAsStringAsync();
            Check.That((int)response.StatusCode==code,$"Equipment {action} expected {code}, got {(int)response.StatusCode}: {text}");
            JsonElement data=default;
            if(text.Length>0){using var doc=JsonDocument.Parse(text);data=doc.RootElement.Clone();}
            if(disposition is not null)Check.That(data.GetProperty("status").GetString()==disposition,"Original confirmed equipment disposition is preserved.");
            Guid? http=null;
            if(enabled)
            {
                http=Guid.Parse(response.Headers.GetValues("X-Game-Request-Id").Single());
                Check.That(response.Headers.GetValues("X-Game-Trace-Id").Single()==Trace,"Equipment uses the actual HTTP Activity.");
            }
            else Check.That(!response.Headers.Contains("X-Game-Request-Id"),"The filter preserves default-off game diagnostics.");
            return (http,data);
        }
        var craft=new EquipmentCraftRequest(Guid.NewGuid(),1,Target,1,[new(tokens.ItemId,"1")]);
        await Send("craft",craft,401,bearer:"invalid-equipment-token");
        await Send("craft",craft,403,bearer:"fixture-admin-token");
        await Send("craft",craft,403,customRoute:$"/api/characters/{foreign.CharacterId}/equipment/");
        await Send("craft",craft with {RuleVersion=99},400);
        var first=await Send("craft",craft,disposition:"Committed");commands.Add(("craft",craft,craft.RequestId,first.Http));
        Guid targetId=first.Body.GetProperty("records").EnumerateArray().Single(r=>r.GetProperty("definitionName").GetString()==Target)
            .GetProperty("state").GetProperty("instanceId").GetGuid();
        var upgrade=new EquipmentUpgradeRequest(Guid.NewGuid(),1,new(starter.ItemId,"1",0),[new(ore.ItemId,"1")]);
        var upgraded=await Send("upgrade",upgrade,disposition:"Committed");commands.Add(("upgrade",upgrade,upgrade.RequestId,upgraded.Http));
        var transfer=new EquipmentTransferRequest(Guid.NewGuid(),1,new(starter.ItemId,"2",1),new(targetId,"1",0));
        var transferred=await Send("transfer",transfer,disposition:"Committed");commands.Add(("transfer",transfer,transfer.RequestId,transferred.Http));
        var secondCraft=craft with {RequestId=Guid.NewGuid(),TokenStacks=[new(tokens.ItemId,"2")]};
        var second=await Send("craft",secondCraft,disposition:"Committed");commands.Add(("craft",secondCraft,secondCraft.RequestId,second.Http));
        Guid secondId=second.Body.GetProperty("records").EnumerateArray().Single(r=>r.GetProperty("definitionName").GetString()==Target)
            .GetProperty("state").GetProperty("instanceId").GetGuid();
        var dismantle=new EquipmentDismantleRequest(Guid.NewGuid(),1,new(secondId,"1",0));
        var dismantled=await Send("dismantle",dismantle,disposition:"Committed");commands.Add(("dismantle",dismantle,dismantle.RequestId,dismantled.Http));
        Check.That((await items.FindAsync(starter.ItemId,default))!.State.EquipmentProgression!.Level==0
            &&(await items.FindAsync(targetId,default))!.State.EquipmentProgression!.Level==1
            &&(await items.FindAsync(secondId,default))!.LifecycleState!=ItemLifecycleState.Active,"Actual upgrade, transfer and dismantle effects are present.");
        var snapshots=new Dictionary<Guid,string>();
        foreach(var command in commands)snapshots[command.Request]=JsonSerializer.Serialize(await items.TryGetCommitResultAsync(command.Request,default));
        string state=JsonSerializer.Serialize((await items.FindByOwnerAsync(owner,true,500,default)).OrderBy(r=>r.ItemId));
        string? fingerprint=pg is null?null:await HostTests.FingerprintAsync(host);
        var attempts=new Dictionary<Guid,Guid?>();
        foreach(var command in commands)
        {
            var replay=await Send(command.Action,command.Body,disposition:"AlreadyCommitted");attempts[command.Request]=replay.Http;
            Check.That(snapshots[command.Request]==JsonSerializer.Serialize(await items.TryGetCommitResultAsync(command.Request,default)),"Historical receipts remain immutable after inputs changed or outputs were consumed.");
        }
        await Send("craft",craft with {TargetDefinitionName="GameItem.Equipment.Progression.Target.CirculationGreatSword"},409);
        await Send("upgrade",upgrade with {RequestId=Guid.NewGuid()},409);
        Check.That(state==JsonSerializer.Serialize((await items.FindByOwnerAsync(owner,true,500,default)).OrderBy(r=>r.ItemId)),"Replay/refusal does not repeat costs, output creation, transfer or dismantle effects.");
        if(enabled&&broken is null)
        {
            using(var denied=await host.Client.GetAsync("/ops/api/logs?transactionRequestId="+craft.RequestId))
                Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"A known equipment receipt gives no diagnostics access.");
            await host.LoginAsync();
            foreach(var command in commands)
            {
                string query=QueryHelpers.AddQueryString("/ops/api/logs",new Dictionary<string,string?>
                { ["transactionRequestId"]=command.Request.ToString("D"),["fromUtc"]=now.AddMinutes(-1).UtcDateTime.ToString("O"),["toUtc"]=now.AddMinutes(1).UtcDateTime.ToString("O") });
                JsonElement[] events=[];
                for(int attempt=0;attempt<10;attempt++)
                {
                    using var response=await host.Client.GetAsync(query);Check.That(response.IsSuccessStatusCode,"Protected equipment log search succeeds.");
                    using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                    events=doc.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Select(e=>e.Clone()).ToArray();
                    if(events.Length==2)break;await Task.Delay(50);
                }
                Check.That(events.Length==2&&events.Select(e=>(Guid?)e.GetProperty("httpRequestId").GetGuid()).ToHashSet().SetEquals([command.Http,attempts[command.Request]])
                    &&events.All(e=>e.GetProperty("characterId").GetGuid()==character.CharacterId&&e.GetProperty("traceId").GetString()==Trace
                        &&e.GetProperty("operationName").GetString()=="Equipment."+char.ToUpperInvariant(command.Action[0])+command.Action[1..]),"Confirmed result links each command/replay to its correct character, distinct HTTP IDs and actual trace.");
            }
        }
        if(fingerprint is not null)Check.That(fingerprint==await HostTests.FingerprintAsync(host),"Replay/refusal/log investigation leaves PG gameplay unchanged after the legitimate commands.");
        await host.StopAsync();
        string raw=string.Join('\n',Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").Select(File.ReadAllText));
        Check.That(!raw.Contains(Secret)&&!raw.Contains(token.Token)&&!raw.Contains(Target)&&!raw.Contains(HostFixture.Account),"No input/header/body, definition, seed fingerprint, account or credential is dumped into logs.");
        var recorded=Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").SelectMany(File.ReadLines).Select(line=>
        {using var doc=JsonDocument.Parse(line);return doc.RootElement.GetProperty("Properties").GetProperty("OpsEvent").Deserialize<LogEvent>(OperationsHttp.Json)!;})
            .Where(e=>e.OperationName.StartsWith("Equipment.",StringComparison.Ordinal)).ToArray();
        if(enabled&&broken is null)
            Check.That(recorded.Length==10&&recorded.Count(e=>e.Outcome=="Committed")==5&&recorded.Count(e=>e.Outcome=="Replayed")==5
                &&recorded.All(e=>e.ItemId is null&&e.DungeonSessionId is null&&e.OperatorId is null&&e.HttpStatus is null),"Only confirmed receipts are observed; multi-item/current-session/transport facts are not inferred.");
        else Check.That(recorded.Length==0&&(broken is null||broken.Seen.Count==10),"Default-off/throwing observers preserve all original equipment commands and historical replays.");
    }

    private static async Task FilterBoundary()
    {
        var capture=new Capture();var filter=new EquipmentReceiptObservationFilter(capture);
        var context=new DefaultEndpointFilterInvocationContext(new DefaultHttpContext());
        object result=Results.StatusCode(403);
        Check.That(ReferenceEquals(result,await filter.InvokeAsync(context,_=>ValueTask.FromResult<object?>(result)))&&capture.Seen.Count==0,"Rejected endpoint result identity is preserved without observation.");
        var failure=new InvalidOperationException(Secret);Exception? seen=null;
        try{await filter.InvokeAsync(context,_=>new ValueTask<object?>(Task.FromException<object?>(failure)));}catch(Exception e){seen=e;}
        Check.That(ReferenceEquals(failure,seen)&&capture.Seen.Count==0,"Business exceptions propagate unchanged outside optional observation handling.");
    }
    private sealed class Capture : IGameTransactionObserver
    {
        internal List<GameTransactionObservation> Seen=[];
        public void Observe(GameTransactionObservation observation){Seen.Add(observation);throw new IOException(Secret);}
    }
}
