using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.ServiceControl;

internal static class ServiceControlTests
{
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        await using(var disabled=await HostFixture.StartAsync(Path.Combine(root,"service-disabled"),enabled:false))
        {
            for(int i=0;i<60;i++)Check.That((await disabled.Client.GetAsync("/api/service-status")).StatusCode==HttpStatusCode.OK,"Public status works without ops cookie or enabled console.");
            Check.That((int)(await disabled.Client.GetAsync("/api/service-status")).StatusCode==429,"Public status is limited to 60 requests/IP/minute.");
            Check.That((await disabled.Client.GetAsync("/api/service-status/child")).StatusCode==HttpStatusCode.Unauthorized,"Anonymous exception is exact path only.");
            Check.That((await disabled.Client.PostAsync("/api/service-status",null)).StatusCode==HttpStatusCode.Unauthorized,"Anonymous exception is GET only.");
        }
        await using(var off=await HostFixture.StartAsync(Path.Combine(root,"service-write-disabled")))
        {await off.LoginAsync();Check.That((await off.Client.GetAsync("/ops/service-control")).StatusCode==HttpStatusCode.NotFound,"Service writes disabled by default.");
         Check.That(!(await off.Client.GetStringAsync("/ops")).Contains("href=\"/ops/service-control\""),"Disabled feature has no navigation link.");}
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
        {
            string hostRoot=Path.Combine(root,pg is null?"service-memory":"service-postgres");
            var host=await HostFixture.StartAsync(hostRoot,pg,serviceControl:true,restrictions:true,
                customizeRoutes:app=>app.MapPost("/api/auth/steam-ticket",async(IGameRepository game)=>
                {await game.UpsertAccountAsync(new(HostFixture.Account,HostFixture.Account,false,false),DateTimeOffset.UtcNow,default);return Results.Ok();}));
            try
            {
                await host.SeedAsync(); await Verify(host);
                if(pg is not null)
                {
                    await Atomicity(host);
                    var cookies=host.Cookies;int port=host.Port;await host.StopAsync();
                    await using var restored=await HostFixture.StartAsync(hostRoot,pg,port,cookies,host.Clock,serviceControl:true);
                    var state=await Read(restored,"/ops/api/service-control");Check.That(state.GetProperty("mode").GetString()=="Drain","Service state survives host restart.");
                    var action=await Read(restored,"/ops/api/actions/"+state.GetProperty("actionRequestId").GetGuid());
                    Check.That(action.GetProperty("target").GetProperty("scope").GetString()=="Service","Immutable service receipt survives restart.");
                }
            }
            finally{await host.DisposeAsync();}
        }
        Console.WriteLine("PASS ServiceControl: public exact GET/rate/privacy, ops CSRF/body/revision/idempotency, atomic admission gates, existing two-member heartbeat/settlement, PG rollback/shared/exclusive/races/restart.");
    }
    private static async Task<JsonElement> Read(HostFixture host,string path)
    {
        using var response=await host.Client.GetAsync(path);string body=await response.Content.ReadAsStringAsync();
        Check.That(response.IsSuccessStatusCode,"Service read: "+body);using var doc=JsonDocument.Parse(body);return doc.RootElement.GetProperty("data").Clone();
    }
    private static async Task<JsonElement> Put(HostFixture host,ServiceIntent value,string? csrf,int expected)
    {
        var dto=new ChangeServiceControlRequest(value.RequestId,value.ExpectedRevision,value.Mode,value.NoticeText,value.NoticeStartsAtUtc,value.NoticeEndsAtUtc,value.Reason);
        using var request=new HttpRequestMessage(HttpMethod.Put,"/ops/api/service-control"){Content=JsonContent.Create(dto,options:OperationsHttp.Json)};
        if(csrf is not null)request.Headers.Add("X-Ops-CSRF",csrf);
        using var response=await host.Client.SendAsync(request);string body=await response.Content.ReadAsStringAsync();
        Check.That((int)response.StatusCode==expected,$"Service PUT expected {expected}, got {(int)response.StatusCode}: {body}");
        using var doc=JsonDocument.Parse(body);return doc.RootElement.TryGetProperty("data",out var data)?data.Clone():doc.RootElement.Clone();
    }
    private static async Task Blocked(Func<Task> call)
    {bool blocked=false;try{await call();}catch(ServiceDrainingException){blocked=true;}Check.That(blocked,"Every new admission boundary rejects Drain.");}
    private static async Task Verify(HostFixture host)
    {
        var game=host.App.Services.GetRequiredService<IGameRepository>();var now=host.Clock.Now;const string peer="76561198099999999";
        await game.UpsertAccountAsync(new(peer,peer,false,false),now,default);
        var a=await game.CreateCharacterAsync(HostFixture.Account,"DrainA"+Guid.NewGuid().ToString("N")[..6],now,default);
        var b=await game.CreateCharacterAsync(peer,"DrainB"+Guid.NewGuid().ToString("N")[..6],now,default);
        var s=await game.CreateDungeonSessionAsync(HostFixture.Account,a.CharacterId,"training","Normal",now,now.AddHours(1),default);
        await game.JoinDungeonSessionAsync(s.DungeonSessionId,peer,b.CharacterId,4,now,now.AddHours(1),default);
        await game.AssignDungeonServerAsync(s.DungeonSessionId,"drain-fixture","127.0.0.1:7777",now,now.AddHours(1),default);
        await game.StartDungeonSessionAsync(s.DungeonSessionId,"drain-fixture",now,now.AddHours(1),default);
        await game.StoreSessionAsync("drain-old-token",HostFixture.Account,now.AddHours(1),default);
        await game.StoreJoinTicketAsync("drain-spare-ticket",HostFixture.Account,a.CharacterId,s.DungeonSessionId,now.AddMinutes(5),default);
        var initial=await Read(host,"/api/service-status");Check.That(initial.GetProperty("mode").GetString()=="Open"&&!initial.TryGetProperty("operatorId",out _),"Initial Open and public metadata excludes operator/audit.");
        var intent=new ServiceIntent(Guid.NewGuid(),0,"Drain","<script>alert('plain notice')</script>",now.AddMinutes(1),now.AddMinutes(2),"scheduled maintenance fixture reason");
        await Put(host,intent,null,401);await host.LoginAsync();
        string csrf=WebUtility.HtmlDecode(Regex.Match(await host.Client.GetStringAsync("/ops"),"name=\"__RequestVerificationToken\"[^>]*value=\"([^\"]+)\"").Groups[1].Value);
        using(var unknown=new HttpRequestMessage(HttpMethod.Put,"/ops/api/service-control"){Content=JsonContent.Create(new {requestId=intent.RequestId,expectedRevision="0",mode="Drain",noticeText="",reason=intent.Reason,isValid=true})})
        {unknown.Headers.Add("X-Ops-CSRF",csrf);using var rejected=await host.Client.SendAsync(unknown);Check.That((int)rejected.StatusCode==400,"Transport DTO rejects unknown fields, including internal policy properties.");}
        await Put(host,intent,null,403);await Put(host,intent with {Mode="Closed"},csrf,400);
        await Put(host,intent with {NoticeEndsAtUtc=now},csrf,400);await Put(host,intent with {Reason="short"},csrf,400);
        await Put(host,intent with {NoticeText=new string('x',40000)},csrf,413);
        var committed=await Put(host,intent,csrf,201);var receipt=committed.GetProperty("receipt");
        var restrictionStore=host.App.Services.GetRequiredService<ProjectRpg.Operations.Core.Restrictions.IAccountRestrictionCommitter>();
        var collision=await restrictionStore.CommitAsync(new(new(intent.RequestId,peer,0,null,"service request collision fixture"),"github:1",Guid.NewGuid(),null),default);
        Check.That(collision.Error?.Code=="ops.request_conflict","Account action cannot reuse a service action ID.");
        Check.That((await Put(host,intent,csrf,200)).GetProperty("receipt").GetRawText()==receipt.GetRawText(),"Replay returns byte-equivalent original receipt.");
        await Put(host,intent with {NoticeText="different"},csrf,409);await Put(host,intent with {RequestId=Guid.NewGuid()},csrf,409);
        var publicState=await Read(host,"/api/service-status");Check.That(publicState.GetProperty("mode").GetString()=="Drain"&&!publicState.GetProperty("noticeVisible").GetBoolean(),"Future notice does not display yet.");
        host.Clock.Now=now.AddMinutes(1);Check.That((await Read(host,"/api/service-status")).GetProperty("noticeVisible").GetBoolean(),"Notice start is inclusive.");
        host.Clock.Now=now.AddMinutes(2);Check.That(!(await Read(host,"/api/service-status")).GetProperty("noticeVisible").GetBoolean(),"Notice end is exclusive.");host.Clock.Now=now;
        using(var response=await host.Client.PostAsync("/api/auth/steam-ticket",null))
        {Check.That((int)response.StatusCode==503&&response.Headers.RetryAfter is not null,"Login maps Drain to 503/Retry-After.");Check.That((await response.Content.ReadAsStringAsync()).Contains("service_draining"),"Safe explicit draining code.");}
        await Blocked(()=>game.StoreSessionAsync("drain-new",HostFixture.Account,now.AddHours(1),default));
        await Blocked(()=>game.CreateDungeonSessionAsync(HostFixture.Account,a.CharacterId,"training","Normal",now,now.AddHours(1),default));
        await Blocked(()=>game.JoinDungeonSessionAsync(Guid.NewGuid(),peer,b.CharacterId,4,now,now.AddHours(1),default));
        await Blocked(()=>game.SetDungeonDepartureAsync(s.DungeonSessionId,HostFixture.Account,a.CharacterId,Guid.NewGuid(),0,false,1,now,default));
        await Blocked(()=>game.StoreJoinTicketAsync("drain-new-ticket",HostFixture.Account,a.CharacterId,s.DungeonSessionId,now.AddMinutes(1),default));
        await Blocked(()=>game.ConsumeJoinTicketAsync("drain-spare-ticket","drain-fixture",s.DungeonSessionId,now,default,Guid.NewGuid()));
        await Blocked(()=>game.ConfirmDungeonMemberAdmissionAsync(s.DungeonSessionId,"drain-fixture",HostFixture.Account,a.CharacterId,1,now,default));
        Check.That(await game.ResolveSessionAsync("drain-old-token",now,default)==HostFixture.Account,"Existing authentication remains usable for reads/mail.");
        var heartbeat=await game.HeartbeatDungeonSessionAsync(s.DungeonSessionId,"drain-fixture",now,now.AddHours(1),default);
        Check.That(heartbeat.State==DungeonSessionState.InProgress&&heartbeat.Members.Count==2,"Drain preserves party, battle and server heartbeat.");
        await game.EnqueueDungeonRewardSettlementAsync(s.DungeonSessionId,"drain-fixture","drain-fixture",new string('d',64),[new("OPS_COIN",1)],[],now,default);
        var observed=new Observer(s.DungeonSessionId);
        using(var worker=new ProjectRpg.Backend.Application.DungeonRewardSettlementWorker(game,host.App.Services.GetRequiredService<IDungeonRewardCommitRepository>(),host.Clock,
            Microsoft.Extensions.Options.Options.Create(new ProjectRpg.Backend.Configuration.DungeonSettlementOptions{PollIntervalMilliseconds=50}),
            Microsoft.Extensions.Logging.Abstractions.NullLogger<ProjectRpg.Backend.Application.DungeonRewardSettlementWorker>.Instance,observed))
        {await worker.StartAsync(default);try{await observed.Done.Task.WaitAsync(TimeSpan.FromSeconds(15));}finally{await worker.StopAsync(default);}}
        Check.That(observed.Characters.SetEquals([a.CharacterId,b.CharacterId]),"Real worker preserves both party rewards during Drain.");
        string html=await host.Client.GetStringAsync("/ops/service-control");Check.That(!html.Contains("<script>alert('plain notice')</script>")&&html.Contains("sessionStorage.setItem"),"Notice renders as plain text; recovery UI present.");
        Check.That((await Read(host,"/ops/api/actions/"+intent.RequestId)).GetProperty("requestId").GetGuid()==intent.RequestId,"Unified receipt lookup includes service action.");
        await Put(host,intent with {RequestId=Guid.NewGuid(),ExpectedRevision=1,Mode="Open",NoticeText="",NoticeStartsAtUtc=null,NoticeEndsAtUtc=null},csrf,201);
        await game.StoreSessionAsync("after-drain-open",HostFixture.Account,now.AddHours(1),default);
        Check.That(!(await Read(host,"/api/service-status")).GetProperty("noticeVisible").GetBoolean(),"Empty notice hides; Open restores admission.");
        var ban=new ProjectRpg.Operations.Core.Restrictions.RestrictionIntent(Guid.NewGuid(),peer,0,null,"reverse request collision fixture");
        Check.That((await restrictionStore.CommitAsync(new(ban,"github:1",Guid.NewGuid(),null),default)).IsSuccess,"Independent account action commits.");
        var reverse=await host.App.Services.GetRequiredService<IServiceControlCommitter>().CommitAsync(new(intent with {RequestId=ban.RequestId,ExpectedRevision=2},"github:1",Guid.NewGuid(),null),default);
        Check.That(reverse.Error?.Code=="ops.request_conflict","Service action cannot reuse an account action ID.");
        await Task.Delay(500);
        string from=Uri.EscapeDataString(now.AddMinutes(-5).UtcDateTime.ToString("O")),to=Uri.EscapeDataString(now.AddMinutes(5).UtcDateTime.ToString("O"));
        var logs=await Read(host,$"/ops/api/logs?operationRequestId={intent.RequestId}&fromUtc={from}&toUtc={to}");
        var events=logs.GetProperty("events").EnumerateArray().ToArray();
        Check.That(events.Any(e=>e.GetProperty("eventName").GetString()=="ops.command.committed")
            &&events.All(e=>e.GetProperty("operationRequestId").GetGuid()==intent.RequestId&&e.GetProperty("traceId").GetString()?.Length==32),"Original operation ID retrieves persisted command and HTTP trace correlation.");
        Check.That(!logs.GetRawText().Contains("plain notice")&&!logs.GetRawText().Contains("scheduled maintenance fixture reason"),"Technical logs exclude command notice/reason bodies.");
    }
    private static async Task Atomicity(HostFixture host)
    {
        var source=host.App.Services.GetRequiredService<NpgsqlDataSource>();var store=host.App.Services.GetRequiredService<IServiceControlCommitter>();
        await using var connection=await source.OpenConnectionAsync();
        await using(var create=new NpgsqlCommand("""
            CREATE FUNCTION service_audit_failure() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN IF NEW.target_scope='Service' THEN RAISE EXCEPTION 'fixture audit failure'; END IF; RETURN NEW; END $$;
            CREATE TRIGGER service_audit_failure BEFORE INSERT ON ops_actions FOR EACH ROW EXECUTE FUNCTION service_audit_failure();
            """,connection))await create.ExecuteNonQueryAsync();
        var command=new ServiceCommand(new(Guid.NewGuid(),2,"Drain","",null,null,"atomic service transition fixture"),"github:1",Guid.NewGuid(),null);
        try{Check.That(!(await store.CommitAsync(command,default)).IsSuccess,"Audit failure rejects transition.");
            Check.That((await host.App.Services.GetRequiredService<IServiceControlReader>().ReadAsync(default)).Value.Mode=="Open","Audit failure rolls back service state.");}
        finally{await using var drop=new NpgsqlCommand("DROP TRIGGER service_audit_failure ON ops_actions; DROP FUNCTION service_audit_failure()",connection);await drop.ExecuteNonQueryAsync();}
        await using(var tx=await connection.BeginTransactionAsync())
        {
            await using(var gate=new NpgsqlCommand("SELECT pg_advisory_xact_lock_shared(1852993381,1)",connection,tx))await gate.ExecuteNonQueryAsync();
            await using(var second=await source.OpenConnectionAsync())await using(var tx2=await second.BeginTransactionAsync())
            {await using var shared=new NpgsqlCommand("SELECT pg_try_advisory_xact_lock_shared(1852993381,1)",second,tx2);Check.That((bool)(await shared.ExecuteScalarAsync())!,"Normal admissions can hold service gate concurrently.");await tx2.RollbackAsync();}
            var pending=store.CommitAsync(command,default);await Task.Delay(100);Check.That(!pending.IsCompleted,"Drain waits for in-flight admission transaction.");
            await tx.RollbackAsync();Check.That((await pending).IsSuccess,"Drain commits after admission transaction finishes.");
        }
        var replay=await Task.WhenAll(Enumerable.Range(0,6).Select(_=>store.CommitAsync(command,default)));
        Check.That(replay.All(r=>r.IsSuccess&&r.Value.Disposition=="Replayed"),"Concurrent same-ID retries never increment revision again.");
        var game=host.App.Services.GetRequiredService<IGameRepository>();await Blocked(()=>game.StoreSessionAsync("after-pg-drain",HostFixture.Account,host.Clock.Now.AddHours(1),default));
    }
    private sealed class Observer(Guid session):ProjectRpg.Backend.Application.IDungeonSettlementObserver
    {
        public TaskCompletionSource Done=new(TaskCreationOptions.RunContinuationsAsynchronously);
        public HashSet<Guid> Characters=[];
        public void Observe(ProjectRpg.Backend.Application.DungeonSettlementObservation value)
        {if(value.SessionId!=session)return;if(value.CharacterId is {} id)Characters.Add(id);
         if(value.Phase==ProjectRpg.Backend.Application.DungeonSettlementPhase.Completed)Done.TrySetResult();
         if(value.Phase==ProjectRpg.Backend.Application.DungeonSettlementPhase.Failed)Done.TrySetException(new Exception("settlement failed"));}
    }
}
