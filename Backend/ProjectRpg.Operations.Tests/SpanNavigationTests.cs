using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.WebUtilities;
using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class SpanNavigationTests
{
    private const string Trace="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    private const string HttpSpan="1111111111111111",ChildSpan="2222222222222222";
    internal static async Task RunAsync()
    {
        Relations();
        using var listener=new ActivityListener {ShouldListenTo=s=>s.Name=="Microsoft.AspNetCore",
            Sample=(ref ActivityCreationOptions<ActivityContext> _)=>ActivitySamplingResult.AllDataAndRecorded};
        ActivitySource.AddActivityListener(listener);
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
            await Http(Path.Combine(root,pg is null?"span-memory":"span-postgres"),pg);
        Console.WriteLine("PASS Span navigation: pure page-local explicit identities, repeated events in one span, environment/trace separation, missing/invalid/ambiguous/cyclic context and 200-row chain; compiled Razor over actual HTTPS item/Memory/PG repository child spans, parent anchors, filtered pagination, authorization and unchanged game state. No cross-page or inferred hierarchy.");
    }

    private static void Relations()
    {
        var root=Event(HttpSpan,null);
        var child=Event(ChildSpan,HttpSpan);
        var rows=OperationsSpanNavigation.Build([root,root with {EventId=Guid.NewGuid(),OperationName="OtherEventSameSpan"},child]);
        Check.That(rows[0].Relation==LogSpanRelation.NoParentRecorded&&rows[2] is {Relation:LogSpanRelation.ParentOnPage,ParentRowId:"log-event-0",ParentEventCount:2},
            "Multiple events sharing a span are not conflicting parents; target is a stable row index, not an untrusted event ID.");
        Check.That(OperationsSpanNavigation.Build([child])[0].Relation==LogSpanRelation.ParentOutsidePage,"A parent on a different page is not guessed or fetched.");
        Check.That(OperationsSpanNavigation.Build([root,child with {Environment="Other"}])[1].Relation==LogSpanRelation.ParentOutsidePage
            &&OperationsSpanNavigation.Build([root,child with {TraceId=new string('b',32)}])[1].Relation==LogSpanRelation.ParentOutsidePage,
            "Same business/parent IDs cannot join different environments or traces.");
        foreach(var malformed in new[]{child with {TraceId=null},child with {TraceId=new string('0',32)},child with {SpanId=new string('0',16)},
            child with {SpanId="<svg onload=x>"},child with {ParentSpanId=new string('A',16)},child with {ParentSpanId=""}})
            Check.That(OperationsSpanNavigation.Build([root,malformed])[1].Relation==LogSpanRelation.InvalidContext,"Only canonical nonzero lower-case W3C span context yields links.");
        foreach(var conflict in new[]{root with {InstanceId="other-process"},root with {Service="OtherService"},root with {BuildVersion="other-build"},root with {ParentSpanId=new string('f',16)}})
        {
            var ambiguous=OperationsSpanNavigation.Build([root,conflict,child]);
            Check.That(ambiguous.All(r=>r.Relation==LogSpanRelation.AmbiguousContext&&r.ParentRowId is null),"Contradictory provenance/parent evidence cannot provide a parent target.");
        }
        Check.That(OperationsSpanNavigation.Build([root with {ParentSpanId=HttpSpan}])[0].Relation==LogSpanRelation.AmbiguousContext,"Self-parent is not a usable relationship.");
        Check.That(OperationsSpanNavigation.Build([root with {ParentSpanId=ChildSpan},child]).All(r=>r.Relation==LogSpanRelation.AmbiguousContext),"Cyclic parent chains do not create navigation loops.");
        var maximum=Enumerable.Range(1,200).Select(i=>Event(i.ToString("x16"),i==1?null:(i-1).ToString("x16"))).ToArray();
        var chain=OperationsSpanNavigation.Build(maximum);
        Check.That(chain.Count==200&&chain.Count(r=>r.Relation==LogSpanRelation.ParentOnPage)==199&&chain[199].ParentRowId=="log-event-198",
            "The full query limit is handled iteratively without dropping deep valid parents.");
        Check.That(OperationsSpanNavigation.Build([]).Count==0,"An empty page adds no inferred relationships.");
    }

    private static LogEvent Event(string span,string? parent)=>new(1,Guid.NewGuid(),"game.storage.returned",DateTimeOffset.UtcNow,
        "Information","OpsTest","Backend","instance","build",Guid.NewGuid(),Trace,span,parent,"GameStorage.ItemCommit",1,
        null,null,null,null,null,Guid.Empty,null,null,"Returned",null,null,"GameStorage");

    private static async Task Http(string root,string? pg)
    {
        await using var host=await HostFixture.StartAsync(root,pg,gameDiagnostics:true,customize:s=>
        {
            s.AddGameRepositoryContracts();s.AddSingleton<ItemOwnerAccessService>();s.AddSingleton<ItemTransactionService>();
        },customizeRoutes:app=>app.MapItemApi());
        await host.SeedAsync();
        var game=host.App.Services.GetRequiredService<IGameRepository>();
        var clock=host.Clock;
        var session=await game.CreateDungeonSessionAsync(HostFixture.Account,host.CharacterId,"training","Normal",clock.Now,clock.Now.AddHours(1),default);
        const string server="span-navigation-fixture";
        await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
        await game.StartDungeonSessionAsync(session.DungeonSessionId,server,clock.Now,clock.Now.AddHours(1),default);
        var token=host.App.Services.GetRequiredService<AccessTokenService>().Issue(TimeSpan.FromHours(1));
        await game.StoreGameServerCredentialAsync(token.TokenHash,server,session.DungeonSessionId,token.ExpiresAt,default);
        Guid transaction=Guid.NewGuid(),item=Guid.NewGuid();string owner=host.CharacterId.ToString("D");
        var contract=new ItemCommitRequestContract(transaction,"SpanNavigation","private-span-command",new("Character",owner),1,
            [new(0,new("RPGItemDefinition","Potion",1,new("Character",owner),new("Inventory",owner,0),new(item,7,1,[],[]),0,"Active",
                new("Unbound",new(5,10),null,"private-span-source",false)))]);
        using var request=new HttpRequestMessage(HttpMethod.Post,"/api/item-transactions/commit") {Content=JsonContent.Create(contract,options:OperationsHttp.Json)};
        request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",token.Token);
        request.Headers.Add("traceparent","00-"+Trace+"-ffffffffffffffff-01");
        using var committed=await host.Client.SendAsync(request);
        Check.That(committed.IsSuccessStatusCode,"Actual item API produced a persisted receipt and repository spans.");
        Guid httpId=Guid.Parse(committed.Headers.GetValues(GameHttpDiagnostics.RequestIdHeader).Single());
        string? before=pg is null?null:await HostTests.FingerprintAsync(host);
        string query=QueryHelpers.AddQueryString("/ops/diagnostics",new Dictionary<string,string?>
        {
            ["httpRequestId"]=httpId.ToString("D"),["sourceId"]="backend",
            ["fromUtc"]=clock.Now.AddMinutes(-1).UtcDateTime.ToString("O"),["toUtc"]=clock.Now.AddMinutes(1).UtcDateTime.ToString("O")
        });
        using(var denied=await host.Client.GetAsync(query))Check.That(denied.StatusCode==HttpStatusCode.Redirect&&denied.Headers.Location?.OriginalString=="/ops/login","Span anchors do not bypass the existing operator login.");
        host.Client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer","fixture-admin-token");
        using(var denied=await host.Client.GetAsync(query))Check.That(denied.StatusCode==HttpStatusCode.Redirect,"Game admin bearer cannot authorize span navigation.");
        host.Client.DefaultRequestHeaders.Authorization=null;
        using var login=await host.LoginAsync();
        JsonElement[] stored=[];
        for(int retry=0;retry<40;retry++)
        {
            using var response=await host.Client.GetAsync(query.Replace("/ops/diagnostics?","/ops/api/logs?",StringComparison.Ordinal));
            Check.That(response.IsSuccessStatusCode,"Protected actual HTTP timeline is available.");
            using var json=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            stored=json.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Select(x=>x.Clone()).ToArray();
            if(stored.Length==5)break;
            await Task.Delay(50);
        }
        Check.That(stored.Length==5,"Page evidence contains two HTTP events, one receipt and two real storage spans.");
        string httpSpan=stored.Single(e=>e.GetProperty("eventName").GetString()=="game.http.started").GetProperty("spanId").GetString()!;
        Check.That(stored.Count(e=>e.GetProperty("spanId").GetString()==httpSpan)==3
            &&stored.Where(e=>e.GetProperty("eventName").GetString()=="game.storage.returned").All(e=>e.GetProperty("parentSpanId").GetString()==httpSpan),
            "The relationship used by the screen is actual recorded parent evidence, not merely shared transaction identity.");
        using(var page=await host.Client.GetAsync(query))
        {
            Check.That(page.IsSuccessStatusCode,"Compiled diagnostics Razor renders the actual trace.");
            string html=await page.Content.ReadAsStringAsync();
            var links=Regex.Matches(html,"<a data-span-parent href=\"#(log-event-[0-9]+)\">");
            Check.That(links.Count==2&&links.Select(m=>m.Groups[1].Value).Distinct().Count()==1,"Both repository spans link to one actual HTTP span row.");
            string target=links[0].Groups[1].Value;
            Check.That(html.Contains("id=\""+target+"\" tabindex=\"-1\" data-span-relation=\"ParentOutsidePage\"")
                &&Regex.Matches(html,"data-span-relation=\"ParentOnPage\"").Count==2,"Incoming remote parent remains outside the page while actual local parent is focusable.");
            string decoded=WebUtility.HtmlDecode(html);
            Check.That(decoded.Contains("이번 페이지 3 건")&&!html.Contains(token.Token)&&!html.Contains("private-span-command")&&!html.Contains("private-span-source"),
                "Repeated HTTP-span events are counted as page events without leaking command/credentials.");
        }
        string filtered=QueryHelpers.AddQueryString(query,new Dictionary<string,string?>{["eventName"]="game.storage.returned",["limit"]="1"});
        using(var page=await host.Client.GetAsync(filtered))
        {
            string html=await page.Content.ReadAsStringAsync();
            Check.That(page.IsSuccessStatusCode&&Regex.Matches(html,"data-span-relation=\"ParentOutsidePage\"").Count==1
                &&!html.Contains("data-span-parent")&&html.Contains("계속 검색"),"A filtered partial page never fabricates a parent link or silently loads other results.");
        }
        if(before is not null)Check.That(before==await HostTests.FingerprintAsync(host),"Span presentation and operator searches leave all gameplay tables unchanged.");
        var itemRecord=await host.App.Services.GetRequiredService<IItemRepository>().FindAsync(item,default);
        Check.That(itemRecord is {Revision:1,State.Quantity:1},"Navigation cannot replay or alter the confirmed item mutation.");
    }
}
