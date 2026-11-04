using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Monitoring;
using ProjectRpg.Operations.Monitor;

internal static class MonitoringHostTests
{
    private const string Token="mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm";
    private const string WebhookToken="fixture-webhook-secret-only";
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        await using(var disabled=await HostFixture.StartAsync(Path.Combine(root,"monitor-disabled")))
            Check.That((await disabled.Client.GetAsync("/internal/operations/status")).StatusCode==HttpStatusCode.NotFound,"Monitor endpoint is disabled by default.");
        foreach(string? postgres in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
        {
            var received=new List<AlertEvent>(); bool rejectDelivery=true;
            await using var host=await HostFixture.StartAsync(Path.Combine(root,postgres is null?"monitor-memory":"monitor-postgres"),postgres,
                clock:new MutableClock { Now=DateTimeOffset.UtcNow.AddHours(1) }, monitoring:true,customizeRoutes:app=>app.MapPost("/test-monitor-webhook",async(HttpContext c)=>
                {
                    Check.That(c.Request.Headers.Authorization=="Bearer "+WebhookToken,"Webhook receives only its dedicated credential.");
                    var value=await c.Request.ReadFromJsonAsync<AlertEvent>(MonitoringJson.Options,c.RequestAborted);
                    Check.That(value is not null,"Webhook receives the typed safe payload."); received.Add(value!);
                    return Results.StatusCode(rejectDelivery?503:204);
                }));
            await host.SeedAsync();
            await AssertAccess(host);
            var game=host.App.Services.GetRequiredService<IGameRepository>();
            var now=host.Clock.Now;
            var session=await game.CreateDungeonSessionAsync(HostFixture.Account,host.CharacterId,"training","Normal",now,now.AddHours(1),default);
            await game.AssignDungeonServerAsync(session.DungeonSessionId,"monitor-fixture","127.0.0.1:7777",now,now.AddHours(1),default);
            await game.StartDungeonSessionAsync(session.DungeonSessionId,"monitor-fixture",now,now.AddHours(1),default);
            await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,"monitor-fixture","fixture",new string('d',64),[],[],now,default);
            var claimed=await game.ClaimNextDungeonRewardSettlementAsync("monitor-fixture",now,now.AddMinutes(5),default);
            // The owned database may have older fixtures. Fail the actual claimed row, then count without inventing IDs.
            Check.That(claimed is not null,"Monitor fixture has pending work.");
            await game.FailDungeonRewardSettlementAsync(claimed!.DungeonSessionId,"monitor-fixture","fixture-secret-error-must-not-appear",now,default);
            var config=new MonitorConfiguration("OpsTest","backend",new Uri(host.Origin),Token,new Uri(host.Origin+"/test-monitor-webhook"),
                WebhookToken,Path.Combine(host.Root,"monitor-state"),1024L*1024*1024);
            config.Validate();
            var probe=new HttpOperationsProbe(host.Client,config,host.Clock);
            string? before=postgres is null?null:await HostTests.FingerprintAsync(host);
            var observation=await probe.ObserveAsync(default);
            Check.That(observation.Status is { Ready:true }&&observation.Status.Settlements.Failed>0,"Real HTTPS machine probe observes persisted settlement aggregates.");
            if(before is not null)Check.That(before==await HostTests.FingerprintAsync(host),"Monitor collection leaves all gameplay tables unchanged.");
            var health=observation.Status!;
            string json=JsonSerializer.Serialize(health,MonitoringJson.Options);
            Check.That(!json.Contains(host.CharacterId.ToString("D"))&&!json.Contains(HostFixture.Account)&&!json.Contains("fixture-secret-error"),
                "Machine status exposes no player IDs, error bodies or receipt contents.");
            using(var store=new FileMonitorStateStore(config.StateDirectory,config.Environment,config.Service))
            {
                var cycle=new MonitorCycle(probe,store,new HttpWebhookDelivery(host.Client,config),config.Environment,config.Service,config.LogBudgetBytes);
                await cycle.ExecuteAsync(default);
                Check.That(received.Count==1&&received[0].IncidentKey=="settlement_failed","End-to-end collection produces an immediate failed-settlement webhook.");
                Guid firstId=received[0].EventId;
                rejectDelivery=false;host.Clock.Now=host.Clock.Now.AddMinutes(1);
                await cycle.ExecuteAsync(default);
                Check.That(received.Count==2&&received[1].EventId==firstId,"Real HTTPS failure retries the same durable event ID.");
                Check.That((await store.LoadAsync(default)).Outbox.Count==0,"Successful webhook removes persisted pending delivery.");
            }
            // A redirect must be a failure; production HttpClient also disables all redirects.
            using(var redirectClient=new HttpClient(new Redirects()))
            {
                Check.That(!(await new HttpWebhookDelivery(redirectClient,config).SendAsync(received[0],default)),"Webhook redirect is not delivery success.");
                var unavailable=await new HttpOperationsProbe(redirectClient,config,host.Clock).ObserveAsync(default);
                Check.That(unavailable.Status is null,"Failed health/status does not reuse the last successful response.");
            }
            for(int i=0;i<31;i++)
            {
                using var response=await Status(host,Token);
                if(response.StatusCode==HttpStatusCode.TooManyRequests)break;
                Check.That(i<30&&response.StatusCode==HttpStatusCode.OK,"Status machine identity has a 30/minute budget.");
            }
        }
        await using(var unavailable=await HostFixture.StartAsync(Path.Combine(root,"monitor-storage-failure"),monitoring:true,
            customize:s=>s.AddSingleton<ISettlementHealthReader,Unavailable>()))
        {
            using var response=await Status(unavailable,Token);
            Check.That(response.StatusCode==HttpStatusCode.ServiceUnavailable&&(await response.Content.ReadAsStringAsync()).Contains("Unknown"),"Storage failure returns Unknown/503 without SQL or exception text.");
        }
        Console.WriteLine("PASS Monitoring HTTPS: separate machine authority, origin/CIDR/method/query/rate boundaries, read-only Memory/PG aggregates, safe payload, webhook retry and storage Unknown.");
    }
    private static async Task<HttpResponseMessage> Status(HostFixture host,string? token,string path="/internal/operations/status",string? authority=null)
    {
        using var request=new HttpRequestMessage(HttpMethod.Get,path);
        if(token is not null)request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",token);
        if(authority is not null)request.Headers.Host=authority;
        return await host.Client.SendAsync(request);
    }
    private static async Task AssertAccess(HostFixture host)
    {
        Check.That((await Status(host,null)).StatusCode==HttpStatusCode.Unauthorized,"Anonymous machine status is denied.");
        Check.That((await Status(host,"fixture-admin-token")).StatusCode==HttpStatusCode.Unauthorized,"Game administrator bearer is not a monitor.");
        Check.That((await Status(host,Token,authority:"invalid.example")).StatusCode==HttpStatusCode.Forbidden,"Monitor requires the configured management origin.");
        Check.That((await host.Client.PostAsync("/internal/operations/status",null)).StatusCode==HttpStatusCode.NotFound,"Only the exact GET is exposed.");
        Check.That((await Status(host,Token,"/internal/operations/status?sql=secret")).StatusCode==HttpStatusCode.BadRequest,"Machine status accepts no free-form query.");
        using(var request=new HttpRequestMessage(HttpMethod.Get,"/ops/api/characters/"+host.CharacterId))
        { request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",Token);Check.That((await host.Client.SendAsync(request)).StatusCode==HttpStatusCode.Unauthorized,"Monitor token cannot access operator APIs."); }
        using(var request=new HttpRequestMessage(HttpMethod.Get,"/api/protected"))
        { request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",Token);Check.That((await host.Client.SendAsync(request)).StatusCode==HttpStatusCode.Unauthorized,"Monitor token cannot access gameplay APIs."); }
        await host.LoginAsync();
        Check.That((await Status(host,null)).StatusCode==HttpStatusCode.Unauthorized,"Operator cookie cannot impersonate the monitor.");
        var options=host.App.Services.GetRequiredService<OperationsOptions>();var original=options.AllowedNetworks;
        try { options.AllowedNetworks=["192.0.2.0/24"];Check.That((await Status(host,Token)).StatusCode==HttpStatusCode.Forbidden,"Valid monitor token still requires the management network."); }
        finally { options.AllowedNetworks=original; }
    }
    private sealed class Redirects : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request,CancellationToken ct)=>
            Task.FromResult(new HttpResponseMessage(HttpStatusCode.Redirect){Headers={Location=new Uri("https://untrusted.example/")}});
    }
    private sealed class Unavailable : ISettlementHealthReader
    { public Task<SettlementHealth> ReadAsync(CancellationToken ct)=>throw new Npgsql.NpgsqlException("fixture-secret-SQL"); }
}
