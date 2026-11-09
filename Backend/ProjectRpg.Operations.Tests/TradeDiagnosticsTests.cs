using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.WebUtilities;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class TradeDiagnosticsTests
{
    private const string Secret="private-trade-76561198077777777@example.invalid";
    private const string Trace="4bf92f3577b34da6a3ce929d0e0e4736";
    private static int sequence;
    private sealed record Person(string Account,Guid Character,string Token);
    private sealed record Seen(Guid Trade,Guid Command,Guid Character,Guid Http,bool Completed,bool Refused);
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        using var listener=new ActivityListener {ShouldListenTo=_=>true,
            Sample=(ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded};
        ActivitySource.AddActivityListener(listener);
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
            await Http(Path.Combine(root,pg is null?"trade-memory":"trade-postgres"),pg);
        await Http(Path.Combine(root,"trade-broken-observer"),null,broken:new Capture());
        await Http(Path.Combine(root,"trade-diagnostics-off"),null,enabled:false);
        Console.WriteLine("PASS Player trade tracing: real bilateral HTTPS/Memory/PG exchange, exact command/completion retries and durable refusal, dedicated trade/command ID search and Razor links, single asset movement, privacy, observer failure and default-off compatibility.");
    }

    private static async Task Http(string root,string? pg,bool enabled=true,Capture? broken=null)
    {
        var policy=new TradeItemPolicy([new("Material","Trade.Trace.Ore",1,true,false,100)]);
        await using var host=await HostFixture.StartAsync(root,pg,gameDiagnostics:enabled,customize:services=>
        {
            services.AddSingleton<IOptions<TradingOptions>>(Options.Create(new TradingOptions {Enabled=true,CurrencyCode="OPS_COIN"}));
            services.AddSingleton<TradeRequestLimiter>();services.AddSingleton<TradeApplication>();
            services.AddSingleton<ITradeSessionRepository>(p=>pg is not null
                ?new PostgresTradeSessionRepository(p.GetRequiredService<NpgsqlDataSource>(),policy)
                :new InMemoryTradeSessionRepository(p.GetRequiredService<InMemoryTransactionGate>(),
                    (InMemoryGameRepository)p.GetRequiredService<IGameRepository>(),p.GetRequiredService<InMemoryItemRepository>(),
                    p.GetRequiredService<InMemoryEconomyRepository>(),policy));
            if(broken is not null)services.AddSingleton<ITradeCommandObserver>(broken);
        },customizeRoutes:app=>app.MapTradeApi());
        await host.SeedAsync();
        var game=host.App.Services.GetRequiredService<IGameRepository>();
        var items=host.App.Services.GetRequiredService<IItemRepository>();
        var economy=host.App.Services.GetRequiredService<IEconomyRepository>();
        async Task<Person> NewPerson()
        {
            string account=(76561198090000000UL+(ulong)Interlocked.Increment(ref sequence)).ToString(System.Globalization.CultureInfo.InvariantCulture);
            await game.UpsertAccountAsync(new(account,account,false,false),host.Clock.Now,default);
            var character=await game.CreateCharacterAsync(account,"Trade"+Guid.NewGuid().ToString("N")[..8],host.Clock.Now,default);
            var token=host.App.Services.GetRequiredService<AccessTokenService>().Issue(TimeSpan.FromHours(1));
            await game.StoreSessionAsync(token.TokenHash,account,token.ExpiresAt,default);
            var result=await economy.CommitAsync(new(Guid.NewGuid(),character.CharacterId,Guid.NewGuid(),"Test.Fund",Secret,Secret,[new("OPS_COIN",100)]),
                (await game.GetCharacterEconomyContextAsync(character.CharacterId,default))!,host.Clock.Now,default);
            Check.That(result.Status==CurrencyTransactionStatus.Committed,"Trade fixture wallet funded.");
            return new(account,character.CharacterId,token.Token);
        }
        var a=await NewPerson();var b=await NewPerson();var eve=await NewPerson();
        var owner=new ItemOwnerRef(ItemOwnerType.Character,a.Character.ToString("D"));
        var item=new ItemRecord("Material","Trade.Trace.Ore",1,owner,new(ItemContainerType.Inventory,owner.OwnerId,30),
            new(Guid.NewGuid(),1,10,[],[]),0,ItemLifecycleState.Active,new(ItemBindState.Unbound,new(0,0),null,"TradeFixture",false));
        Check.That((await items.CommitAsync(new(Guid.NewGuid(),"Test.Seed",Secret,owner,10,[new(0,item)]),host.Clock.Now,default)).Status
            ==ItemRepositoryCommitStatus.Committed,"Trade fixture item seeded.");
        Guid trade=Guid.NewGuid();var observed=new List<Seen>();
        async Task<JsonElement> Send(Person actor,Guid id,string action,object body,int code=200,string? bearer=null)
        {
            using var request=new HttpRequestMessage(HttpMethod.Post,$"/api/characters/{actor.Character}/trades/{id}/{action}")
                {Content=JsonContent.Create(body,body.GetType(),options:OperationsHttp.Json)};
            request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",bearer??actor.Token);
            request.Headers.Add("traceparent","00-"+Trace+"-00f067aa0ba902b7-01");request.Headers.Add("X-Private-Input",Secret);
            using var response=await host.Client.SendAsync(request);string text=await response.Content.ReadAsStringAsync();
            Check.That((int)response.StatusCode==code,$"Trade {action} expected {code}, got {(int)response.StatusCode}: {text}");
            JsonElement result=default;if(text.Length>0){using var doc=JsonDocument.Parse(text);result=doc.RootElement.Clone();}
            if(enabled)
            {
                Guid http=Guid.Parse(response.Headers.GetValues("X-Game-Request-Id").Single());
                Check.That(response.Headers.GetValues("X-Game-Trace-Id").Single()==Trace,"Trade retains the actual HTTP trace.");
                if(result.ValueKind==JsonValueKind.Object&&result.TryGetProperty("requestId",out var requestId))
                    observed.Add(new(id,requestId.GetGuid(),actor.Character,http,result.GetProperty("session").GetProperty("completed").GetBoolean(),
                        result.GetProperty("rejection").ValueKind!=JsonValueKind.Null));
            }
            else Check.That(!response.Headers.Contains("X-Game-Request-Id"),"Trade works without optional diagnostics.");
            return result;
        }
        static object Command(JsonElement s)=>new {requestId=Guid.NewGuid(),expectedRevision=s.GetProperty("revision").GetString()};
        static object Consent(JsonElement s)=>new {requestId=Guid.NewGuid(),expectedRevision=s.GetProperty("revision").GetString(),expectedOfferRevision=s.GetProperty("offerRevision").GetString()};
        var invite=new {requestId=Guid.NewGuid(),inviteeCharacterId=b.Character};
        await Send(a,trade,"invite",invite,401,"invalid-trade-token");await Send(a,trade,"invite",invite,403,"fixture-admin-token");
        await Send(a,trade,"invite",invite,403,eve.Token);
        await Send(a,trade,"invite",new {requestId=Guid.NewGuid(),inviteeCharacterId=b.Character,privateInput=Secret},400);
        var first=await Send(a,trade,"invite",invite);
        Check.That(first.GetRawText()==(await Send(a,trade,"invite",invite)).GetRawText(),"Invite receipt retry preserves the original stored result.");
        var s=(await Send(b,trade,"accept",Command(first.GetProperty("session")))).GetProperty("session");
        var offer=new {requestId=Guid.NewGuid(),expectedRevision=s.GetProperty("revision").GetString(),
            offer=new {items=new[]{new {itemId=item.ItemId,expectedRevision="1",quantity=3}},currencyAmount="20"}};
        s=(await Send(a,trade,"offer",offer)).GetProperty("session");
        s=(await Send(a,trade,"ready",Consent(s))).GetProperty("session");
        s=(await Send(b,trade,"ready",Consent(s))).GetProperty("session");
        s=(await Send(a,trade,"confirm",Consent(s))).GetProperty("session");
        Check.That(!s.GetProperty("completed").GetBoolean()&&await Balance(a)==100&&await Balance(b)==100,"First confirmation is not falsely observed as assets exchanged.");
        var final=Consent(s);var completed=await Send(b,trade,"confirm",final);
        Check.That(completed.GetProperty("session").GetProperty("completed").GetBoolean(),"Actual bilateral atomic exchange completed.");
        string state=await AssetState();
        Check.That(completed.GetRawText()==(await Send(b,trade,"confirm",final)).GetRawText()&&state==await AssetState(),"Completed receipt retry never moves items or currency a second time.");
        await Send(b,trade,"cancel",new {requestId=completed.GetProperty("requestId").GetGuid(),expectedRevision=s.GetProperty("revision").GetString()},409);
        Check.That(await Balance(a)==80&&await Balance(b)==120&&(await items.FindAsync(item.ItemId,default))!.State.Quantity==7
            &&(await items.FindByOwnerAsync(new(ItemOwnerType.Character,b.Character.ToString("D")),false,500,default)).Any(i=>i.DefinitionName==item.DefinitionName&&i.State.Quantity==3),"Actual item split and currency movement occurred once.");
        Guid refusedTrade=Guid.NewGuid();
        s=(await Send(a,refusedTrade,"invite",new {requestId=Guid.NewGuid(),inviteeCharacterId=b.Character})).GetProperty("session");
        s=(await Send(b,refusedTrade,"accept",Command(s))).GetProperty("session");
        s=(await Send(a,refusedTrade,"offer",new {requestId=Guid.NewGuid(),expectedRevision=s.GetProperty("revision").GetString(),
            offer=new {items=Array.Empty<object>(),currencyAmount="1000"}})).GetProperty("session");
        var failedReady=Consent(s);var refused=await Send(a,refusedTrade,"ready",failedReady,409);
        Check.That(refused.TryGetProperty("requestId",out var refusedId)&&refused.GetProperty("rejection").ValueKind==JsonValueKind.String
            &&!refused.GetProperty("session").GetProperty("completed").GetBoolean(),"A durable refusal is distinguished from an exchange or an unpersisted exception.");
        string? fingerprint=pg is null?null:await HostTests.FingerprintAsync(host);
        Check.That(refused.GetRawText()==(await Send(a,refusedTrade,"ready",failedReady,409)).GetRawText()&&state==await AssetState(),"Durable refusal retry preserves receipt and assets.");
        if(enabled&&broken is null)
        {
            Check.That(observed.Count==14&&observed.Select(o=>o.Http).Distinct().Count()==14,"All persisted command observations have independent HTTP attempts.");
            using(var denied=await host.Client.GetAsync("/ops/api/logs?tradeId="+trade))Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"Trade IDs grant no operator authority.");
            await host.LoginAsync();
            foreach(var (field,id,count) in new[]{("tradeId",trade,9),("tradeCommandRequestId",completed.GetProperty("requestId").GetGuid(),2),
                ("tradeId",refusedTrade,5),("tradeCommandRequestId",refusedId.GetGuid(),2)})
            {
                string query=Query(field,id);JsonElement[] events=[];
                for(int attempt=0;attempt<10;attempt++)
                {
                    using var response=await host.Client.GetAsync(query);Check.That(response.IsSuccessStatusCode,"New ID filters use the existing authorized bounded search.");
                    using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync());events=doc.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Select(e=>e.Clone()).ToArray();
                    if(events.Length==count)break;await Task.Delay(50);
                }
                Check.That(events.Length==count&&events.All(e=>e.GetProperty("traceId").GetString()==Trace&&observed.Any(o=>o.Http==e.GetProperty("httpRequestId").GetGuid()
                    &&o.Trade==e.GetProperty("tradeId").GetGuid()&&o.Command==e.GetProperty("tradeCommandRequestId").GetGuid()
                    &&o.Character==e.GetProperty("characterId").GetGuid())),"Stored trade/command/verified actor IDs resolve to their actual HTTP attempts and trace.");
            }
            using var page=await host.Client.GetAsync(Query("tradeId",trade).Replace("/ops/api/logs","/ops/diagnostics",StringComparison.Ordinal));
            string html=await page.Content.ReadAsStringAsync();Check.That(page.IsSuccessStatusCode&&html.Contains("data-log-filter=\"tradeId\"")&&html.Contains("data-log-filter=\"tradeCommandRequestId\""),"Actual trade observations render dedicated connection links.");
        }
        if(fingerprint is not null)Check.That(fingerprint==await HostTests.FingerprintAsync(host),"Replay and operator diagnostics do not mutate game or trade tables.");
        await host.StopAsync();
        string raw=string.Join('\n',Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").Select(File.ReadAllText));
        Check.That(!raw.Contains(Secret)&&!raw.Contains(a.Token)&&!raw.Contains(a.Account)&&!raw.Contains(b.Account)&&!raw.Contains(item.DefinitionName),"No account, credential, header, offer or item definition payload enters the observer log.");
        var entries=Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").SelectMany(File.ReadLines).Select(line=>
        {using var doc=JsonDocument.Parse(line);return doc.RootElement.GetProperty("Properties").GetProperty("OpsEvent").Deserialize<LogEvent>(OperationsHttp.Json)!;})
            .Where(e=>e.EventName=="game.trade.command.observed").ToArray();
        if(enabled&&broken is null)
            Check.That(entries.Length==14&&entries.Count(e=>e.Outcome=="CompletedReceiptObserved")==2&&entries.Count(e=>e.Outcome=="StoredRefusal")==2
                &&entries.All(e=>e.OperationRequestId is null&&e.TransactionRequestId is null&&e.DungeonSessionId is null&&e.ItemId is null&&e.HttpStatus is null),"Observations are not mislabeled as new transfers, generic item transactions, dungeon sessions or transport completion.");
        else Check.That(entries.Length==0&&(broken is null||broken.Seen.Count==14),"Throwing or absent observers preserve real exchange, refusals and retries.");

        string Query(string field,Guid id)=>QueryHelpers.AddQueryString("/ops/api/logs",new Dictionary<string,string?>
        { [field]=id.ToString("D"),["fromUtc"]=host.Clock.Now.AddMinutes(-1).UtcDateTime.ToString("O"),["toUtc"]=host.Clock.Now.AddMinutes(1).UtcDateTime.ToString("O") });
        async Task<long> Balance(Person p)=>(await economy.LoadWalletAsync((await game.GetCharacterEconomyContextAsync(p.Character,default))!,default)).Balances.Single(x=>x.Definition.CurrencyCode=="OPS_COIN").Balance;
        async Task<string> AssetState()=>JsonSerializer.Serialize(new {a=await Balance(a),b=await Balance(b),items=(await items.FindByOwnerAsync(owner,true,500,default)).OrderBy(i=>i.ItemId),
            received=(await items.FindByOwnerAsync(new(ItemOwnerType.Character,b.Character.ToString("D")),true,500,default)).OrderBy(i=>i.ItemId)});
    }

    private sealed class Capture : ITradeCommandObserver
    {
        internal List<TradeCommandObservation> Seen=[];
        public void Observe(TradeCommandObservation observation){Seen.Add(observation);throw new IOException(Secret);}
    }
}
