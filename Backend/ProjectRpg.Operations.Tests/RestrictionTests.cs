using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Npgsql;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Restrictions;

internal static class RestrictionTests
{
    private const string Other="76561198098765432";
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        await using(var disabled=await HostFixture.StartAsync(Path.Combine(root,"restriction-disabled")))
        {await disabled.LoginAsync();Check.That((await disabled.Client.GetAsync("/ops/api/account-restrictions/"+HostFixture.Account)).StatusCode==HttpStatusCode.NotFound,"Restrictions disabled by default.");}
        foreach(string? pg in new[]{(string?)null,Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES")}.Distinct())
        {
            var clock=new MutableClock {Now=DateTimeOffset.UtcNow.AddHours(2)};
            string hostRoot=Path.Combine(root,pg is null?"restriction-memory":"restriction-postgres");
            var host=await HostFixture.StartAsync(hostRoot,pg,clock:clock,restrictions:true,compensation:true,
                customizeRoutes:app=>app.MapPost("/api/auth/steam-ticket",async(IGameRepository game)=>
                {await game.UpsertAccountAsync(new(HostFixture.Account,HostFixture.Account,false,false),clock.Now,default);return Results.Ok();}));
            try
            {
                await host.SeedAsync();await Verify(host);
                if(pg is not null)
                {
                    await AtomicityAndRaces(host);
                    using var scope=host.App.Services.CreateScope();
                    var command=new RestrictionIntent(Guid.NewGuid(),Other,0,null,"durable restriction fixture reason");
                    var result=await scope.ServiceProvider.GetRequiredService<IAccountRestrictionCommitter>().CommitAsync(new(command,"github:1",Guid.NewGuid(),null),default);
                    Check.That(result.IsSuccess,"Durable restriction commits before restart: "+result.Error?.Code);
                    var cookies=host.Cookies;int port=host.Port;await host.StopAsync();
                    await using var restored=await HostFixture.StartAsync(hostRoot,pg,port,cookies,clock,restrictions:true,compensation:true);
                    var state=await Get(restored,"/ops/api/account-restrictions/"+Other);
                    Check.That(state.GetProperty("state").GetString()=="Active","Restriction persists across host restart.");
                    var replay=await Post(restored,"/ops/api/account-restrictions",new RestrictAccountRequest(command.RequestId,Other,0,null,command.Reason),await Csrf(restored),200);
                    Check.That(replay.GetProperty("receipt").GetProperty("requestId").GetGuid()==command.RequestId,"Original operation replays after restart.");
                }
            }
            finally{await host.DisposeAsync();}
        }
        Console.WriteLine("PASS Restrictions: HTTP auth/CSRF/body, revisions/idempotency/expiry/release, auth/session/admission guards, existing combat/rewards, durable audit and PG rollback/concurrency.");
    }
    private static async Task<string> Csrf(HostFixture host)=>WebUtility.HtmlDecode(Regex.Match(await host.Client.GetStringAsync("/ops"),"name=\"__RequestVerificationToken\"[^>]*value=\"([^\"]+)\"").Groups[1].Value);
    private static async Task<JsonElement> Post(HostFixture host,string path,object value,string? csrf,int code)
    {
        using var request=new HttpRequestMessage(HttpMethod.Post,path){Content=JsonContent.Create(value,options:OperationsHttp.Json)};
        if(csrf is not null)request.Headers.Add("X-Ops-CSRF",csrf);
        using var response=await host.Client.SendAsync(request);string body=await response.Content.ReadAsStringAsync();
        Check.That((int)response.StatusCode==code,$"Restriction expected {code}, got {(int)response.StatusCode}: {body}");
        using var doc=JsonDocument.Parse(body);return doc.RootElement.TryGetProperty("data",out var data)?data.Clone():doc.RootElement.Clone();
    }
    private static async Task<JsonElement> Get(HostFixture host,string path)
    {
        using var response=await host.Client.GetAsync(path);string body=await response.Content.ReadAsStringAsync();
        Check.That(response.IsSuccessStatusCode,"Restriction read failed: "+body);
        using var doc=JsonDocument.Parse(body);return doc.RootElement.GetProperty("data").Clone();
    }
    private static async Task Player(HostFixture host,string token,int code)
    {
        using var request=new HttpRequestMessage(HttpMethod.Get,"/api/protected");request.Headers.Authorization=new("Bearer",token);
        using var response=await host.Client.SendAsync(request);Check.That((int)response.StatusCode==code,"Expected gameplay authentication "+code);
    }
    private static async Task Denied(Func<Task> action)
    {bool denied=false;try{await action();}catch(AccountRestrictedException){denied=true;}Check.That(denied,"Restricted account cannot cross a new admission/session boundary.");}
    private static async Task Verify(HostFixture host)
    {
        var game=host.App.Services.GetRequiredService<IGameRepository>();var tokens=host.App.Services.GetRequiredService<AccessTokenService>();
        var now=host.Clock.Now;
        await game.UpsertAccountAsync(new(Other,Other,false,false),now,default);
        var target=await game.CreateCharacterAsync(HostFixture.Account,"Ban"+Guid.NewGuid().ToString("N")[..10],now,default);
        var other=await game.CreateCharacterAsync(Other,"Peer"+Guid.NewGuid().ToString("N")[..10],now,default);
        var session=await game.CreateDungeonSessionAsync(HostFixture.Account,target.CharacterId,"training","Normal",now,now.AddHours(1),default);
        await game.JoinDungeonSessionAsync(session.DungeonSessionId,Other,other.CharacterId,4,now,now.AddHours(1),default);
        await game.AssignDungeonServerAsync(session.DungeonSessionId,"restriction-fixture","127.0.0.1:7777",now,now.AddHours(1),default);
        await game.StartDungeonSessionAsync(session.DungeonSessionId,"restriction-fixture",now,now.AddHours(1),default);
        var server=tokens.Issue(TimeSpan.FromHours(1));await game.StoreGameServerCredentialAsync(server.TokenHash,"restriction-fixture",session.DungeonSessionId,server.ExpiresAt,default);
        var player=tokens.Issue(TimeSpan.FromHours(1));await game.StoreSessionAsync(player.TokenHash,HostFixture.Account,player.ExpiresAt,default);
        var peer=tokens.Issue(TimeSpan.FromHours(1));await game.StoreSessionAsync(peer.TokenHash,Other,peer.ExpiresAt,default);
        var ticket=tokens.Issue(TimeSpan.FromMinutes(5));
        Check.That(await game.StoreJoinTicketAsync(ticket.TokenHash,HostFixture.Account,target.CharacterId,session.DungeonSessionId,ticket.ExpiresAt,default)==JoinTicketIssueResult.Issued,"Pre-restriction ticket issues.");
        Guid consumeId=Guid.NewGuid();var admitted=(await game.ConsumeJoinTicketAsync(ticket.TokenHash,"restriction-fixture",session.DungeonSessionId,now,default,consumeId))!;
        Check.That(await game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId,"restriction-fixture",HostFixture.Account,target.CharacterId,admitted.ConnectionGeneration,now,default),"Existing battle admission confirmed.");
        var spare=tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreJoinTicketAsync(spare.TokenHash,HostFixture.Account,target.CharacterId,session.DungeonSessionId,spare.ExpiresAt,default);
        var request=new RestrictAccountRequest(Guid.NewGuid(),HostFixture.Account,0,now.AddMinutes(1),"  investigated account restriction fixture  ");
        await Post(host,"/ops/api/account-restrictions",request,null,401);
        await host.LoginAsync();string csrf=await Csrf(host);
        await Post(host,"/ops/api/account-restrictions",request,null,403);
        await Post(host,"/ops/api/account-restrictions",request with {Reason="short"},csrf,400);
        await Post(host,"/ops/api/account-restrictions",request with {Reason="😀😀😀😀😀"},csrf,400);
        await Post(host,"/ops/api/account-restrictions",request with {ExpiresAtUtc=now.AddDays(31)},csrf,400);
        await Post(host,"/ops/api/account-restrictions",request with {Reason=new string('x',40000)},csrf,413);
        var none=await Get(host,"/ops/api/account-restrictions/"+HostFixture.Account);
        Check.That(none.GetProperty("state").GetString()=="None"&&none.GetProperty("revision").GetString()=="0","Account without a restriction is None/revision zero.");
        await Post(host,"/ops/api/account-restrictions/"+Other+"/release",new ReleaseAccountRequest(Guid.NewGuid(),0,"release missing restriction fixture reason"),csrf,404);
        var committed=await Post(host,"/ops/api/account-restrictions",request,csrf,201);var receipt=committed.GetProperty("receipt");
        Check.That(receipt.GetProperty("result").GetProperty("revokedSessionCount").GetInt32()==1,"Restriction atomically revokes only target gameplay sessions.");
        await Player(host,player.Token,401);await Player(host,peer.Token,200);await Player(host,server.Token,200);
        using(var blockedLogin=await host.Client.PostAsync("/api/auth/steam-ticket",null))Check.That(blockedLogin.StatusCode==HttpStatusCode.Forbidden,"Restricted login maps to an explicit safe 403 response.");
        await Denied(()=>game.StoreSessionAsync("blocked-session",HostFixture.Account,now.AddHours(1),default));
        await Denied(()=>game.UpsertAccountAsync(new(HostFixture.Account,HostFixture.Account,false,false),now,default));
        await Denied(()=>game.CreateDungeonSessionAsync(HostFixture.Account,target.CharacterId,"training","Normal",now,now.AddHours(1),default));
        await Denied(()=>game.JoinDungeonSessionAsync(Guid.NewGuid(),HostFixture.Account,target.CharacterId,4,now,now.AddHours(1),default));
        await Denied(()=>game.StoreJoinTicketAsync("new-ticket",HostFixture.Account,target.CharacterId,session.DungeonSessionId,now.AddMinutes(1),default));
        await Denied(()=>game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId,"restriction-fixture",HostFixture.Account,target.CharacterId,admitted.ConnectionGeneration,now,default));
        Check.That(await game.ConsumeJoinTicketAsync(spare.TokenHash,"restriction-fixture",session.DungeonSessionId,now,default,Guid.NewGuid()) is null,"Unused pre-ban ticket cannot admit after restriction.");
        Check.That(await game.ConsumeJoinTicketAsync(ticket.TokenHash,"restriction-fixture",session.DungeonSessionId,now,default,consumeId) is null,"Old consume request cannot be used for banned re-admission.");
        Check.That(await game.ReadJoinTicketConsumptionEvidenceAsync(ticket.TokenHash,"restriction-fixture",session.DungeonSessionId,consumeId,HostFixture.Account,now,default) is not null,"Restriction preserves immutable admission evidence.");
        var heartbeat=await game.HeartbeatDungeonSessionAsync(session.DungeonSessionId,"restriction-fixture",now,now.AddHours(1),default);
        Check.That(heartbeat.Members.Count==2,"Restriction preserves existing battle party and heartbeat.");
        await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,"restriction-fixture","restriction-fixture",new string('f',64),[new("OPS_COIN",1)],[],now,default);
        var observed=new SettlementObserver(session.DungeonSessionId);
        using(var worker=new ProjectRpg.Backend.Application.DungeonRewardSettlementWorker(game,
            host.App.Services.GetRequiredService<IDungeonRewardCommitRepository>(),host.Clock,
            Microsoft.Extensions.Options.Options.Create(new ProjectRpg.Backend.Configuration.DungeonSettlementOptions {PollIntervalMilliseconds=50}),
            Microsoft.Extensions.Logging.Abstractions.NullLogger<ProjectRpg.Backend.Application.DungeonRewardSettlementWorker>.Instance,observed))
        {
            await worker.StartAsync(default);
            try {await observed.Done.Task.WaitAsync(TimeSpan.FromSeconds(15));}finally{await worker.StopAsync(default);}
        }
        Check.That(observed.Characters.Contains(target.CharacterId)&&observed.Characters.Contains(other.CharacterId),"Real worker commits rewards for restricted and remaining party members.");
        var replay=await Post(host,"/ops/api/account-restrictions",request,csrf,200);
        Check.That(replay.GetProperty("receipt").GetRawText()==receipt.GetRawText(),"Same request replays original receipt, not current state.");
        await Post(host,"/ops/api/account-restrictions",request with {Reason="different investigated restriction reason"},csrf,409);
        await Post(host,"/ops/api/account-restrictions",request with {RequestId=Guid.NewGuid()},csrf,409);
        var direct=await Get(host,"/ops/api/actions/"+request.RequestId);Check.That(direct.GetProperty("target").GetProperty("id").GetString()==HostFixture.Account,"Unified action lookup returns the account receipt.");
        host.Clock.Now=host.Clock.Now.AddMinutes(2);
        var expired=await Get(host,"/ops/api/account-restrictions/"+HostFixture.Account);
        Check.That(expired.GetProperty("state").GetString()=="Expired"&&expired.GetProperty("revision").GetString()=="1","Expiry is observed without mutating revision.");
        await game.StoreSessionAsync("after-expiry",HostFixture.Account,now.AddHours(1),default);
        Check.That(await game.ResolveSessionAsync(player.TokenHash,host.Clock.Now,default) is null,"Expiry never resurrects revoked authentication.");
        var release=new ReleaseAccountRequest(Guid.NewGuid(),1,"release after investigated expiration");
        await Post(host,"/ops/api/account-restrictions/"+HostFixture.Account+"/release",release,csrf,201);
        Check.That(await game.ResolveSessionAsync("after-expiry",host.Clock.Now,default)==HostFixture.Account,"Release does not revoke a legitimate new post-expiry session.");
        await Post(host,"/ops/api/account-restrictions",request,csrf,200);
        var history=await Get(host,"/ops/api/actions?targetScope=Account&targetId="+HostFixture.Account+"&limit=1");
        Check.That(history.GetProperty("hasMore").GetBoolean(),"Account action history uses bounded keyset pagination.");
        var compensation=host.App.Services.GetRequiredService<ProjectRpg.Operations.Core.Compensation.ICompensationCommitter>();
        var policy=host.App.Services.GetRequiredService<ProjectRpg.Operations.Core.Compensation.ICompensationPolicy>();
        var preview=new ProjectRpg.Operations.Core.Compensation.CompensationPreview(
            new(target.CharacterId,Guid.NewGuid(),ProjectRpg.Operations.Core.Compensation.CompensationIntent.Template,policy.AllowedDefinitions[0],"cross feature request identity fixture"),
            "github:1",policy.Version,host.Clock.Now,host.Clock.Now.AddMinutes(5));
        var collided=await compensation.CommitAsync(new(request.RequestId,preview,Guid.NewGuid(),null),default);
        Check.That(collided.Error?.Code=="ops.request_conflict","Compensation cannot reuse an account action ID.");
        var grantId=Guid.NewGuid();Check.That((await compensation.CommitAsync(new(grantId,preview,Guid.NewGuid(),null),default)).IsSuccess,"Independent compensation still commits after restriction release.");
        var restrictionStore=host.App.Services.GetRequiredService<IAccountRestrictionCommitter>();
        var reverse=await restrictionStore.CommitAsync(new(new(grantId,Other,0,null,"cross feature restriction fixture reason"),"github:1",Guid.NewGuid(),null),default);
        Check.That(reverse.Error?.Code=="ops.request_conflict","Restriction cannot reuse a compensation action ID.");
        var originalGrant=await Get(host,"/ops/api/actions/"+grantId);
        Check.That(originalGrant.GetProperty("target").GetProperty("scope").GetString()=="Character","Unified action lookup still returns compensation receipts.");
        string html=await host.Client.GetStringAsync("/ops/accounts/"+HostFixture.Account+"/restrictions");
        Check.That(html.Contains("localStorage.setItem")&&html.Contains("sessionStorage.setItem")&&html.Contains("X-Ops-CSRF"),"Operator page renders request recovery and CSRF workflow.");
    }
    private static async Task AtomicityAndRaces(HostFixture host)
    {
        var game=host.App.Services.GetRequiredService<IGameRepository>();var store=host.App.Services.GetRequiredService<IAccountRestrictionCommitter>();
        var source=host.App.Services.GetRequiredService<NpgsqlDataSource>();const string account="76561198033333333";
        await game.UpsertAccountAsync(new(account,account,false,false),host.Clock.Now,default);
        await game.StoreSessionAsync("rollback-original",account,host.Clock.Now.AddHours(1),default);
        RestrictionCommand Command()=>new(new(Guid.NewGuid(),account,0,null,"atomic restriction race fixture reason"),"github:1",Guid.NewGuid(),null);
        var command=Command();await using var connection=await source.OpenConnectionAsync();
        bool invalidShape=false;
        await using(var shape=new NpgsqlCommand("""
            INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,case_id,target_scope,target_id,reason,committed_at,result_json,origin_http_request_id)
            SELECT @request,'invalid-shape',decode(repeat('00',32),'hex'),'{}','fixture',@case,'Character',character_id,'invalid shape fixture',now(),'{}',@request
            FROM characters LIMIT 1
            """,connection))
        {
            shape.Parameters.AddWithValue("request",Guid.NewGuid());shape.Parameters.AddWithValue("case",Guid.NewGuid());
            try{await shape.ExecuteNonQueryAsync();}catch(PostgresException e)when(e.SqlState==PostgresErrorCodes.CheckViolation){invalidShape=true;}
        }
        Check.That(invalidShape,"Account audit migration preserves strict character template constraints, including SQL NULL semantics.");
        await using(var fault=new NpgsqlCommand("""
            CREATE FUNCTION ops_restriction_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'fixture rollback'; END $$;
            CREATE TRIGGER ops_restriction_fault BEFORE INSERT ON ops_actions FOR EACH ROW EXECUTE FUNCTION ops_restriction_test_fail();
            """,connection))await fault.ExecuteNonQueryAsync();
        try
        {
            Check.That(!(await store.CommitAsync(command,default)).IsSuccess,"Injected audit failure refuses partial restriction.");
            Check.That(await game.ResolveSessionAsync("rollback-original",host.Clock.Now,default)==account,"Audit failure rolls back session revocation.");
            Check.That((await host.App.Services.GetRequiredService<IAccountRestrictionReader>().FindAsync(account,default)).Value.State=="None","Audit failure rolls back restriction state.");
        }
        finally{await using var cleanup=new NpgsqlCommand("DROP TRIGGER ops_restriction_fault ON ops_actions; DROP FUNCTION ops_restriction_test_fail();",connection);await cleanup.ExecuteNonQueryAsync();}
        var results=await Task.WhenAll(Enumerable.Range(0,5).Select(_=>store.CommitAsync(command,default)));
        Check.That(results.All(r=>r.IsSuccess)&&results.Count(r=>r.Value.Disposition=="Committed")==1,"Concurrent same requests commit exactly once.");
        const string race="76561198044444444";await game.UpsertAccountAsync(new(race,race,false,false),host.Clock.Now,default);
        var ban=new RestrictionCommand(new(Guid.NewGuid(),race,0,null,"admission and login concurrent restriction"),"github:1",Guid.NewGuid(),null);
        await using(var tx=await connection.BeginTransactionAsync())
        {
            await using(var gate=new NpgsqlCommand("SELECT pg_advisory_xact_lock(hashtextextended(@key,0))",connection,tx))
            {gate.Parameters.AddWithValue("key","ops_account:"+race);await gate.ExecuteNonQueryAsync();}
            var restricted=store.CommitAsync(ban,default);
            var login=Task.Run(async()=>{try{await game.StoreSessionAsync("race-session",race,host.Clock.Now.AddHours(1),default);}catch(AccountRestrictedException){}});
            await Task.Delay(100);Check.That(!restricted.IsCompleted&&!login.IsCompleted,"Login and restriction wait on the same account transaction gate.");
            await tx.RollbackAsync();Check.That((await restricted).IsSuccess,"Restriction wins or revokes a login that linearized first.");await login;
        }
        Check.That(await game.ResolveSessionAsync("race-session",host.Clock.Now,default) is null,"No valid login survives the ban/login race.");
    }
    private sealed class SettlementObserver(Guid session):ProjectRpg.Backend.Application.IDungeonSettlementObserver
    {
        public TaskCompletionSource Done=new(TaskCreationOptions.RunContinuationsAsynchronously);
        public HashSet<Guid> Characters=[];
        public void Observe(ProjectRpg.Backend.Application.DungeonSettlementObservation observation)
        {
            if(observation.SessionId!=session)return;
            if(observation.Phase==ProjectRpg.Backend.Application.DungeonSettlementPhase.ReceiptCommitted&&observation.CharacterId is {} id)Characters.Add(id);
            if(observation.Phase==ProjectRpg.Backend.Application.DungeonSettlementPhase.Completed)Done.TrySetResult();
            if(observation.Phase==ProjectRpg.Backend.Application.DungeonSettlementPhase.Failed)Done.TrySetException(new InvalidOperationException("Restricted party settlement failed."));
        }
    }
}
