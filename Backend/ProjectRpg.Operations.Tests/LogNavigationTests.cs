using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class LogNavigationTests
{
    private const string Trace = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    private const string UnsafeText = "<img src=x onerror=alert('log-fixture')>";

    internal static async Task RunAsync()
    {
        Check.That(OperationsLogNavigation.IdentifierFields.Select(f=>f.Key).ToHashSet().SetEquals(OperationLogQuery.IdFields),
            "The screen exposes every registered ID filter without inventing unsupported ones.");
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        using var listener=new ActivityListener { ShouldListenTo=_=>true,
            Sample=(ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded };
        ActivitySource.AddActivityListener(listener);
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
            await Http(Path.Combine(root,pg is null?"navigation-memory":"navigation-postgres"),pg);
        Console.WriteLine("PASS Log navigation: compiled Razor over real HTTPS/JSONL, every ID and registered event, source/time-preserving fresh pivots, bound continuation, auth, HTML escaping and read-only Memory/PG; rich relationship rows are synthetic fixtures.");
    }

    private static async Task Http(string root,string? pg)
    {
        await using var host=await HostFixture.StartAsync(root,pg,gameDiagnostics:true);
        await host.SeedAsync();
        string? before=pg is null?null:await HostTests.FingerprintAsync(host);
        DateTimeOffset originalNow=host.Clock.Now;
        var newest=Fixture(host.CharacterId,host.SessionId,originalNow.AddSeconds(-1));
        var older=newest with { EventId=Guid.NewGuid(),HttpRequestId=Guid.NewGuid(),OperationName="FirstFixture",TimestampUtc=originalNow.AddSeconds(-2) };
        var transport=newest with { EventId=Guid.NewGuid(),EventName="game.http.completed",TransactionRequestId=null,
            OperationName="TransportFixture",HttpRequestId=Guid.NewGuid(),HttpStatus=503,Outcome="Rejected",TimestampUtc=originalNow.AddMilliseconds(-500) };
        string fixture=Path.Combine(root,"logs","ops-20990101.jsonl");
        await File.WriteAllTextAsync(fixture,Line(older)+Line(newest)+Line(transport),new UTF8Encoding(false));
        string initial=QueryHelpers.AddQueryString("/ops/diagnostics",new Dictionary<string,string?>
        {
            ["transactionRequestId"]=newest.TransactionRequestId!.Value.ToString("D"),["eventName"]=newest.EventName,
            ["level"]="Information",["sourceId"]="backend",["limit"]="1"
        });
        using(var denied=await host.Client.GetAsync(initial))Check.That(denied.StatusCode==HttpStatusCode.Redirect&&denied.Headers.Location?.OriginalString=="/ops/login","Known log identifiers lead to the existing page login boundary.");
        host.Client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer","fixture-admin-token");
        using(var denied=await host.Client.GetAsync(initial))Check.That(denied.StatusCode==HttpStatusCode.Redirect&&denied.Headers.Location?.OriginalString=="/ops/login","Game administrator bearer does not authorize diagnostics navigation.");
        host.Client.DefaultRequestHeaders.Authorization=null;
        await host.LoginAsync();
        string html=await Page(host,initial);
        foreach(var field in OperationsLogNavigation.IdentifierFields)
            Check.That(html.Contains("name=\""+field.Key+"\""),"All supported ID inputs are rendered.");
        foreach(string name in OperationLogQuery.EventNames)
            Check.That(html.Contains("<option value=\""+name+"\">"),"Event suggestions use the current registry, including game HTTP/receipts/worker events.");
        Check.That(!html.Contains(UnsafeText)&&WebUtility.HtmlDecode(html).Contains(UnsafeText),"Stored operation/build text is HTML encoded, not executed as markup.");
        var links=Links(html);
        Check.That(links.Count==8&&links.Select(l=>l.Key).ToHashSet().SetEquals(OperationLogQuery.IdFields),"Each present confirmed ID has one supported local navigation link.");
        var period=QueryHelpers.ParseQuery(new Uri(host.Origin+links[0].Url).Query);
        string from=period["fromUtc"].ToString(),to=period["toUtc"].ToString();
        Check.That(from==originalNow.AddHours(-1).UtcDateTime.ToString("O")&&to==originalNow.UtcDateTime.ToString("O"),
            "Implicit default search period is frozen to the actual searched UTC period.");
        foreach(var link in links)
        {
            Check.That(link.Url.StartsWith("/ops/diagnostics?",StringComparison.Ordinal),"Navigation always targets the same authorized local page.");
            var query=QueryHelpers.ParseQuery(new Uri(host.Origin+link.Url).Query);
            Check.That(query.Count==4&&query.ContainsKey(link.Key)&&query["sourceId"]=="backend"&&query["fromUtc"]==from&&query["toUtc"]==to,
                "A pivot keeps source/period and one selected identity; old AND filters, limit and cursor are absent.");
        }
        string next=Next(html);
        var nextQuery=QueryHelpers.ParseQuery(new Uri(host.Origin+next).Query);
        Check.That(nextQuery["eventName"]==newest.EventName&&nextQuery["transactionRequestId"]==newest.TransactionRequestId.Value.ToString("D")
            &&nextQuery["level"]=="Information"&&nextQuery["limit"]=="1"&&nextQuery["cursor"].Count==1
            &&nextQuery["fromUtc"]==from&&nextQuery["toUtc"]==to,"Continuation retains its original filters, page size, period and bound cursor.");
        var late=newest with { EventId=Guid.NewGuid(),OperationName="AppendedLater",TimestampUtc=originalNow.AddMilliseconds(-200) };
        await File.AppendAllTextAsync(fixture,Line(late),new UTF8Encoding(false));
        host.Clock.Now=host.Clock.Now.AddMinutes(5);
        await Page(host,links.Single(l=>l.Key=="httpRequestId").Url);
        string tracePage=await Page(host,links.Single(l=>l.Key=="traceId").Url);
        Check.That(tracePage.Contains("TransportFixture")&&tracePage.Contains("HTTP 503")&&tracePage.Contains("AppendedLater"),
            "Fresh trace search broadens beyond receipt-only filters and sees newly appended matching records in the original period.");
        string continuation=await Page(host,next);
        Check.That(continuation.Contains("FirstFixture")&&!continuation.Contains("AppendedLater"),"Continuation retains frozen JSONL EOF and does not show newly appended lines.");
        string mismatched=QueryHelpers.AddQueryString(links.Single(l=>l.Key=="traceId").Url,"cursor",nextQuery["cursor"].ToString());
        await Page(host,mismatched,400);
        await Page(host,"/ops/diagnostics?traceId="+Trace+"&sourceId=..%2Fprivate",400);
        string malicious=QueryHelpers.AddQueryString("/ops/diagnostics","traceId",UnsafeText);
        string rejected=await Page(host,malicious,400);
        Check.That(!rejected.Contains(UnsafeText)&&WebUtility.HtmlDecode(rejected).Contains(UnsafeText),"Rejected query text remains encoded in form attributes.");

        // Also follow a persisted event generated by the real game HTTP boundary, separate from rich synthetic rows.
        using(var response=await host.Client.GetAsync("/api/service-status"))
        {
            string requestId=response.Headers.GetValues("X-Game-Request-Id").Single();
            string actualTrace=response.Headers.GetValues("X-Game-Trace-Id").Single();
            string real=QueryHelpers.AddQueryString("/ops/diagnostics",new Dictionary<string,string?>
            {
                ["httpRequestId"]=requestId,["fromUtc"]=host.Clock.Now.AddMinutes(-1).UtcDateTime.ToString("O"),
                ["toUtc"]=host.Clock.Now.AddMinutes(1).UtcDateTime.ToString("O")
            });
            // Wait for the owned async sink without spending the production 10-searches/minute allowance on polling.
            for(int attempt=0;attempt<40;attempt++)
            {
                if(StoredGameHttp(root,requestId))break;
                await Task.Delay(50);
            }
            string realHtml=await Page(host,real);
            var actualLinks=Links(realHtml);
            Check.That(realHtml.Contains("game.http.completed")&&actualLinks.Any(l=>l.Key=="traceId"
                &&QueryHelpers.ParseQuery(new Uri(host.Origin+l.Url).Query)["traceId"]==actualTrace),"Actual response request ID resolves to its real persisted trace link.");
            await Page(host,actualLinks.First(l=>l.Key=="traceId").Url);
        }
        if(before is not null)Check.That(before==await HostTests.FingerprintAsync(host),"Diagnostics forms, pivots, continuation and real status query leave gameplay tables unchanged.");
        var page=new WebLogPage([newest],null,LogCoverage.Complete,null,["backend"],0,originalNow.AddHours(-1),originalNow);
        Check.That(!OperationsLogNavigation.Connections(newest with { TraceId="javascript:alert(1)",ItemId=Guid.Empty },page)
            .Any(l=>l.Key is "traceId" or "itemId"),"Malformed or empty IDs are not turned into invented navigable identities.");
        Check.That(OperationsLogNavigation.Connections(newest,page with { SearchedSources=[] }).Count==0
            &&OperationsLogNavigation.Connections(newest,page with { SearchedSources=["one","two"] }).Count==0,
            "Missing or ambiguous source context is not guessed for navigation.");
    }

    private static LogEvent Fixture(Guid character,Guid session,DateTimeOffset at)=>new(1,Guid.NewGuid(),"game.transaction.receipt.replayed",at,
        "Information","OpsTest","Backend.fixture","fixture-process",UnsafeText,Guid.NewGuid(),Trace,new string('b',16),new string('c',16),
        UnsafeText,null,null,character,null,session,Guid.NewGuid(),Guid.NewGuid(),Guid.NewGuid(),Guid.NewGuid(),"Replayed",null,null,"NavigationFixture");
    private static string Line(LogEvent entry)=>JsonSerializer.Serialize(new { Properties=new { OpsEvent=JsonSerializer.SerializeToElement(entry,OperationsHttp.Json) } })+"\n";
    private static bool StoredGameHttp(string root,string id)
    {
        foreach(string file in Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl"))
        {
            using var stream=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
            using var reader=new StreamReader(stream);
            while(reader.ReadLine() is { } line)
                if(line.Contains(id,StringComparison.Ordinal)&&line.Contains("game.http.completed",StringComparison.Ordinal))return true;
        }
        return false;
    }
    private static async Task<string> Page(HostFixture host,string url,int status=200)
    {
        using var response=await host.Client.GetAsync(url);
        string html=await response.Content.ReadAsStringAsync();
        Check.That((int)response.StatusCode==status,$"Diagnostics page expected {status}, got {(int)response.StatusCode}.");
        return html;
    }
    private static List<(string Key,string Url)> Links(string html)=>Regex.Matches(html,"<a data-log-filter=\"([^\"]+)\" href=\"([^\"]+)\">")
        .Select(m=>(m.Groups[1].Value,WebUtility.HtmlDecode(m.Groups[2].Value))).ToList();
    private static string Next(string html)
    {
        // Continuation is the only diagnostics query link without data-log-filter; the layout link targets /ops/diagnostics without query.
        var found=Regex.Matches(html,"<a href=\"([^\"]+)\">").Select(m=>WebUtility.HtmlDecode(m.Groups[1].Value))
            .Single(url=>url.StartsWith("/ops/diagnostics?",StringComparison.Ordinal)&&QueryHelpers.ParseQuery(new Uri("https://fixture.invalid"+url).Query).ContainsKey("cursor"));
        return found;
    }
}
