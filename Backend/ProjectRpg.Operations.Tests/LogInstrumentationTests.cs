using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class LogInstrumentationTests
{
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        using var listener=new ActivityListener {ShouldListenTo=_=>true,
            Sample=(ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded};
        ActivitySource.AddActivityListener(listener);
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
            await Restart(Path.Combine(root,pg is null?"instrumentation-memory":"instrumentation-postgres"),pg);
        Console.WriteLine("PASS Instrumentation disclosure: real HTTPS/JSONL off-on-off restarts, authorized current process/configuration metadata, retained enabled-period events visible while current configuration is off, history remains Unknown, initial Razor screen and PG gameplay unchanged.");
    }

    private static async Task Restart(string root,string? pg)
    {
        MutableClock clock=new();Guid requestId=Guid.Empty;string? actualTrace=null;
        var boots=new HashSet<string>(StringComparer.Ordinal);
        string? fingerprint=null;
        foreach(int phase in new[]{0,1,2})
        {
            bool enabled=phase==1;
            await using var host=await HostFixture.StartAsync(root,pg,clock:clock,gameDiagnostics:enabled);
            if(phase==0)
            {
                await host.SeedAsync();
                if(pg is not null)fingerprint=await HostTests.FingerprintAsync(host);
            }
            string Query()=>QueryHelpers.AddQueryString("/ops/api/logs",new Dictionary<string,string?>
            {
                [requestId==Guid.Empty?"eventName":"httpRequestId"]=requestId==Guid.Empty?"game.http.completed":requestId.ToString("D"),
                ["fromUtc"]=clock.Now.AddMinutes(-1).UtcDateTime.ToString("O"),["toUtc"]=clock.Now.AddMinutes(1).UtcDateTime.ToString("O")
            });
            using(var denied=await host.Client.GetAsync(Query()))
            {
                Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"Anonymous metadata lookup is still a protected log search.");
                Check.That(!(await denied.Content.ReadAsStringAsync()).Contains("instrumentation",StringComparison.OrdinalIgnoreCase),"Denied JSON does not disclose instrumentation configuration.");
            }
            host.Client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer","fixture-admin-token");
            using(var denied=await host.Client.GetAsync(Query()))Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"Game administrator is not an operator instrumentation reader.");
            host.Client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer",new string('m',48));
            using(var denied=await host.Client.GetAsync(Query()))Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"Monitor token cannot read operator log metadata.");
            host.Client.DefaultRequestHeaders.Authorization=null;
            await host.LoginAsync();
            using(var initial=await host.Client.GetAsync("/ops/diagnostics"))
            {
                Check.That(initial.IsSuccessStatusCode,"Initial authorized log screen displays configured producers before a search.");
                string html=await initial.Content.ReadAsStringAsync();
                foreach(string key in new[]{"game-http","game-transactions","player-trade","game-storage","settlement"})
                {
                    bool configured=key=="settlement"||enabled;
                    Check.That(html.Contains($"data-log-producer=\"{key}\" data-configured-enabled=\"{configured.ToString().ToLowerInvariant()}\""),"Razor renders the actual configured state for each supported producer group.");
                }
                string decoded=WebUtility.HtmlDecode(html);
                Check.That(decoded.Contains("검색 기간 전체의 계측 이력은 알 수 없으며")&&decoded.Contains("무손실 수집이나 저장 성공을 보장하지 않습니다"),"Screen separates configured producers, historical coverage and delivery success.");
            }
            using(var status=await host.Client.GetAsync("/api/service-status"))
            {
                if(enabled)
                {
                    requestId=Guid.Parse(status.Headers.GetValues("X-Game-Request-Id").Single());
                    actualTrace=status.Headers.GetValues("X-Game-Trace-Id").Single();
                }
                else Check.That(!status.Headers.Contains("X-Game-Request-Id"),"Current disabled configuration matches actual game HTTP behavior.");
            }
            JsonElement data=default;string raw="";
            for(int attempt=0;attempt<10;attempt++)
            {
                using var response=await host.Client.GetAsync(Query());
                Check.That(response.IsSuccessStatusCode,"Authorized log search returns source and current instrumentation metadata.");
                raw=await response.Content.ReadAsStringAsync();
                using var doc=JsonDocument.Parse(raw);data=doc.RootElement.GetProperty("data").Clone();
                if(!enabled||data.GetProperty("events").GetArrayLength()==2)break;
                await Task.Delay(50);
            }
            var metadata=data.GetProperty("instrumentation");
            Check.That(metadata.GetProperty("scope").GetString()=="CurrentProcessOnly"&&metadata.GetProperty("historicalCoverage").GetString()=="Unknown",
                "Search metadata never represents current configuration as historical collection coverage.");
            Check.That(metadata.GetProperty("environment").GetString()=="OpsTest"&&metadata.GetProperty("sourceId").GetString()=="backend"
                &&metadata.GetProperty("observedAtUtc").GetDateTimeOffset()==clock.Now&&metadata.GetProperty("buildVersion").GetString() is {Length:>0},"Configuration snapshot identifies the actual source/build and observation time.");
            string boot=metadata.GetProperty("bootId").GetString()!;
            Check.That(Guid.TryParseExact(boot,"N",out var bootId)&&bootId!=Guid.Empty&&boots.Add(boot),"Each restarted process is distinguished from the prior event source.");
            var producers=metadata.GetProperty("producers").EnumerateArray().ToArray();
            Check.That(producers.Length==5&&producers.Select(p=>p.GetProperty("key").GetString()).Distinct().Count()==5,"Bounded metadata exposes the implemented configuration groups.");
            foreach(var producer in producers)
            {
                string key=producer.GetProperty("key").GetString()!;
                Check.That(producer.GetProperty("configuredEnabled").GetBoolean()==(key=="settlement"||enabled),"Disclosure follows actual current game diagnostics configuration and separate settlement registration.");
                Check.That(producer.GetProperty("eventNames").EnumerateArray().All(e=>OperationLogQuery.EventNames.Contains(e.GetString()!)),"Producer metadata uses only registered event names.");
                Check.That(producer.GetProperty("boundaries").GetArrayLength() is >0 and <=7,"Only bounded implemented boundaries are disclosed.");
            }
            var events=data.GetProperty("events").EnumerateArray().ToArray();
            if(phase==0)Check.That(events.Length==0,"No game HTTP events are fabricated while diagnostics is disabled.");
            else
                Check.That(events.Length==2&&events.All(e=>e.GetProperty("httpRequestId").GetGuid()==requestId&&e.GetProperty("traceId").GetString()==actualTrace),
                    "Real enabled-period HTTP events remain searchable when the next process disables instrumentation.");
            if(phase==2)Check.That(events.All(e=>e.GetProperty("instanceId").GetString()!=boot),"Historical event process identity is not overwritten with the current snapshot identity.");
            Check.That(!raw.Contains(host.Root)&&!raw.Contains("fixture-only-no-real-secret")&&!raw.Contains("fixture-admin-token")
                &&!raw.Contains("operations-test-epoch"),"Instrumentation metadata excludes local paths, credentials and authentication epoch.");
            if(fingerprint is not null)Check.That(fingerprint==await HostTests.FingerprintAsync(host),"Configuration disclosure and retained log search across restarts do not mutate gameplay tables.");
        }
    }
}
