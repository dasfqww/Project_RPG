using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class GameHttpDiagnosticsTests
{
    private const string Secret="private-game-payload-76561198055555555@example.invalid";
    internal static async Task RunAsync()
    {
        await FailureIsolation();
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        await using(var off=await HostFixture.StartAsync(Path.Combine(root,"game-http-off")))
        {
            using var response=await off.Client.GetAsync("/api/service-status");
            Check.That(!response.Headers.Contains(GameHttpDiagnostics.RequestIdHeader),"Game diagnostics is off by default.");
        }
        bool invalid=false;try {new OperationsOptions {GameApiDiagnosticsEnabled=true}.Validate(null!);}catch(InvalidOperationException){invalid=true;}
        Check.That(invalid,"Game diagnostics cannot silently run without its configured operations sink.");
        using var listener=new ActivityListener { ShouldListenTo=_=>true,
            Sample=(ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded };
        ActivitySource.AddActivityListener(listener);
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
            await Http(Path.Combine(root,pg is null?"game-http-memory":"game-http-postgres"),pg);
        Console.WriteLine("PASS Game HTTP diagnostics: server-owned IDs, W3C correlation, concurrent/retry scopes, persisted protected search, static route masking, body/header privacy, optional sink isolation and read-only Memory/PG integration.");
    }

    private static async Task Http(string root,string? pg)
    {
        await using var host=await HostFixture.StartAsync(root,pg,gameDiagnostics:true,customizeRoutes:app=>
        {
            app.MapPost("/api/diagnostics-fixture/{opaque}",(HttpContext c)=>
            {c.Response.Headers[GameHttpDiagnostics.RequestIdHeader]="handler-cannot-forge";return Results.Json(new {result=Secret},statusCode:201);});
            app.MapGet("/api/diagnostics-drain",()=>Task.FromException<IResult>(new ServiceDrainingException()));
        });
        await host.SeedAsync();
        string? before=pg is null?null:await HostTests.FingerprintAsync(host);
        var requests=new System.Collections.Concurrent.ConcurrentBag<(Guid Id,int Status,string Operation)>();
        async Task<Guid> Send(string verb,string path,bool authorized=true,string? trace=null)
        {
            using var request=new HttpRequestMessage(new HttpMethod(verb),path);
            request.Headers.TryAddWithoutValidation(GameHttpDiagnostics.RequestIdHeader,"caller-cannot-forge");
            request.Headers.TryAddWithoutValidation(GameHttpDiagnostics.TraceIdHeader,"caller-trace-secret");
            request.Headers.TryAddWithoutValidation("X-Private-Input",Secret);
            if(authorized)request.Headers.Authorization=new AuthenticationHeaderValue("Bearer","fixture-admin-token");
            if(trace is not null)request.Headers.TryAddWithoutValidation("traceparent","00-"+trace+"-00f067aa0ba902b7-01");
            if(verb=="POST")request.Content=new StringContent("{\"ticket\":\""+Secret+"\",\"requestId\":\"same-business-id\"}",Encoding.UTF8,"application/json");
            using var response=await host.Client.SendAsync(request);
            string id=response.Headers.GetValues(GameHttpDiagnostics.RequestIdHeader).Single();
            Check.That(Guid.TryParseExact(id,"D",out var parsed)&&parsed!=Guid.Empty,"Every accepted API attempt has a fresh server-generated GUID.");
            string responseTrace=response.Headers.GetValues(GameHttpDiagnostics.TraceIdHeader).Single();
            Check.That(responseTrace.Length==32&&responseTrace!="caller-trace-secret"&&(trace is null||responseTrace==trace),"Response exposes the actual server Activity, never a custom caller trace number.");
            if(verb=="POST")Check.That((int)response.StatusCode==201&&(await response.Content.ReadAsStringAsync()).Contains(Secret),"Diagnostics preserves the handler body/status.");
            string operation=path.StartsWith("/api/diagnostics-fixture/",StringComparison.Ordinal)?"POST /api/diagnostics-fixture/{opaque}":verb+" "+path.Split('?')[0];
            requests.Add((parsed,(int)response.StatusCode,operation));return parsed;
        }
        const string parentTrace="4bf92f3577b34da6a3ce929d0e0e4736";
        Guid first=await Send("POST","/api/diagnostics-fixture/"+Uri.EscapeDataString(Secret)+"?ticket="+Uri.EscapeDataString(Secret),trace:parentTrace);
        Guid retry=await Send("POST","/api/diagnostics-fixture/retry");
        Check.That(first!=retry,"Same logical request/body retry is a different HTTP attempt.");
        await Send("GET","/api/protected",authorized:false);
        await Send("GET","/api/service-status");
        await Send("GET","/api/diagnostics-drain");
        // Parallel calls use separate scopes, while the log writes remain optional.
        var parallel=await Task.WhenAll(Enumerable.Range(0,8).Select(_=>Send("GET","/api/service-status")));
        Check.That(parallel.Distinct().Count()==8&&requests.Select(x=>x.Id).Distinct().Count()==requests.Count,"Concurrent attempts never share request IDs.");
        if(before is not null)Check.That(before==await HostTests.FingerprintAsync(host),"Diagnostic requests and log writes leave gameplay tables unchanged.");
        using(var denied=await host.Client.GetAsync("/ops/api/logs?httpRequestId="+first))
            Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"Knowing a game trace number grants no diagnostics permission.");
        await host.LoginAsync();
        string from=Uri.EscapeDataString(host.Clock.Now.AddMinutes(-1).UtcDateTime.ToString("O"));
        string to=Uri.EscapeDataString(host.Clock.Now.AddMinutes(1).UtcDateTime.ToString("O"));
        JsonElement[] events=[];
        for(int attempt=0;attempt<30;attempt++)
        {
            using var found=await host.Client.GetAsync($"/ops/api/logs?httpRequestId={first}&fromUtc={from}&toUtc={to}");
            Check.That(found.IsSuccessStatusCode,"Operator searches the same persisted source by game HTTP request number.");
            using var doc=JsonDocument.Parse(await found.Content.ReadAsStringAsync());
            events=doc.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Select(x=>x.Clone()).ToArray();
            if(events.Length==2)break;
            await Task.Delay(50);
        }
        Check.That(events.Length==2&&events.All(e=>e.GetProperty("traceId").GetString()==parentTrace),"Actual request headers resolve to persisted started/completed events and W3C trace.");
        using(var outside=await host.Client.GetAsync("/ops"))
            Check.That(!outside.Headers.Contains(GameHttpDiagnostics.RequestIdHeader),"Operations requests do not acquire duplicate game-attempt headers.");
        await host.StopAsync(); // Flush owned async JSONL before byte-level privacy and complete-attempt checks.
        string text=string.Join('\n',Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl").Select(File.ReadAllText));
        Check.That(!text.Contains(Secret)&&!text.Contains("fixture-admin-token")&&!text.Contains("caller-cannot-forge")
            &&!text.Contains("caller-trace-secret")&&!text.Contains("same-business-id"),"Stored diagnostics excludes path values, query, request/response bodies and credential/custom headers.");
        var game=ReadEvents(root);
        foreach(var request in requests)
        {
            var pair=game.Where(e=>e.HttpRequestId==request.Id).ToArray();
            Check.That(pair.Length==2&&pair.Count(e=>e.EventName=="game.http.started")==1,"Each HTTP attempt has one start and one completion.");
            var complete=pair.Single(e=>e.EventName=="game.http.completed");
            Check.That(complete.HttpStatus==request.Status&&complete.OperationName==request.Operation&&complete.DurationMs>=0
                &&complete.CharacterId is null&&complete.OperatorId is null&&complete.OperationRequestId is null,
                "Static route/status/duration are real transport facts; unverified actors and business IDs stay absent.");
        }
        Check.That(game.Any(e=>e.HttpStatus==401&&e.Outcome=="Rejected")&&game.Any(e=>e.HttpStatus==503&&e.Outcome=="Rejected"),"Authentication and admission refusals retain their final HTTP outcome.");
    }

    private static List<LogEvent> ReadEvents(string root)
    {
        var events=new List<LogEvent>();
        foreach(string file in Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl"))
        foreach(string line in File.ReadLines(file))
        {
            using var doc=JsonDocument.Parse(line);
            var value=doc.RootElement.GetProperty("Properties").GetProperty("OpsEvent");
            var item=value.Deserialize<LogEvent>(ProjectRpg.Backend.Operations.Web.OperationsHttp.Json)!;
            if(item.EventName.StartsWith("game.http.",StringComparison.Ordinal))events.Add(item);
        }
        return events;
    }

    private static async Task FailureIsolation()
    {
        var context=new DefaultHttpContext();context.Request.Path="/api/unit";
        var capture=new Observer();var fault=new InvalidOperationException(Secret);
        Exception? seen=null;try{await new GameHttpDiagnostics(_=>throw fault).InvokeAsync(context,capture);}catch(Exception e){seen=e;}
        Check.That(ReferenceEquals(seen,fault)&&capture.Seen.Last().Outcome==GameHttpOutcome.Faulted&&capture.Seen.Last().HttpStatus==500,
            "Faulted handlers rethrow their original exception and record a bounded transport failure.");
        Check.That(!JsonSerializer.Serialize(capture.Seen).Contains(Secret),"Exception text never crosses the observation port.");
        var broken=new Observer {Throw=true};context=new();context.Request.Path="/api/unit";
        await new GameHttpDiagnostics(c=>{c.Response.StatusCode=204;return Task.CompletedTask;}).InvokeAsync(context,broken);
        Check.That(context.Response.StatusCode==204&&broken.Calls==2,"Sink failure cannot alter the handler status or block completion.");
        using var canceled=new CancellationTokenSource();canceled.Cancel();context=new();context.Request.Path="/api/unit";context.RequestAborted=canceled.Token;
        capture=new();try{await new GameHttpDiagnostics(_=>throw new OperationCanceledException(Secret)).InvokeAsync(context,capture);}catch(OperationCanceledException){}
        Check.That(capture.Seen.Last().Outcome==GameHttpOutcome.Canceled&&capture.Seen.Last().HttpStatus is null,"Cancellation is not fabricated as HTTP200/499 or success.");
    }
    private sealed class Observer : IGameHttpObserver
    {
        internal bool Throw;internal int Calls;internal List<GameHttpObservation> Seen=[];
        public void Observe(GameHttpObservation observation){Calls++;if(Throw)throw new IOException(Secret);Seen.Add(observation);}
    }
}
