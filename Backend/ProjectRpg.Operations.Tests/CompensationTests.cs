using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.Extensions.DependencyInjection;
using Npgsql;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Compensation;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Compensation;

internal static class CompensationTests
{
    private static readonly string Ore = CompensationCatalog.Key(FirstSliceEquipmentDefinitions.OreDefinitionName);
    internal static async Task RunAsync()
    {
        string root = Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        bool rejected = false;
        try { _ = new CompensationCatalog(new OperationsOptions { Compensation = new() { Enabled=true, AllowedDefinitions=["unregistered:1"] } }); }
        catch (InvalidOperationException) { rejected=true; }
        Check.That(rejected, "Unsupported server recipe/version fails startup validation.");
        await using (var empty = await HostFixture.StartAsync(Path.Combine(root, "comp-empty"), compensation:true, allowedDefinitions:[]))
        {
            await empty.SeedAsync(); await empty.LoginAsync();
            var csrf = await Csrf(empty);
            await Post(empty, "/ops/api/compensations/preview", Intent(empty.CharacterId), csrf, 403, "ops.compensation_not_allowed");
        }
        await using (var memory = await HostFixture.StartAsync(Path.Combine(root, "comp-memory"), compensation:true))
        { await memory.SeedAsync(); await Verify(memory); }
        string? postgres = Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES");
        if (postgres is not null)
        {
            string pgRoot = Path.Combine(root, "comp-postgres");
            var pg = await HostFixture.StartAsync(pgRoot, postgres, compensation:true);
            try
            {
                await pg.SeedAsync();
                var saved = await Verify(pg);
                await PostgresAtomicity(pg);
                var cookies = pg.Cookies; var clock = pg.Clock; int port = pg.Port;
                await pg.StopAsync();
                await using (var restored = await HostFixture.StartAsync(pgRoot, postgres, port, cookies, clock, compensation:true, allowedDefinitions:[]))
                {
                    using var receipt = await Get(restored, "/ops/api/actions/" + saved.RequestId);
                    Check.That(receipt.RootElement.GetProperty("data").GetProperty("result").GetProperty("itemId").GetGuid() == saved.ItemId, "Durable audit and original item survive host restart/policy removal.");
                    await Post(restored, "/ops/api/compensations", new CompensationSubmission(saved.RequestId, saved.Token), await Csrf(restored), 200);
                    var lostKeys = new CompensationPreviewTokens(new EphemeralDataProtectionProvider(), restored.App.Services.GetRequiredService<OperationsOptions>());
                    Check.That(!lostKeys.Read(saved.Token).IsSuccess, "Lost preview key fails closed; original action lookup remains available.");
                }
            }
            finally { await pg.DisposeAsync(); }
        }
        Console.WriteLine("PASS Compensation: authenticated HTTPS/CSRF, bounded bodies, policy/preview, mail-only atomic receipts, replay/conflict/case, busy state, pagination and recovery.");
    }
    private sealed record Saved(Guid RequestId, Guid TargetId, Guid ItemId, string Token);
    private static CompensationIntent Intent(Guid target, Guid? caseId = null) => new(target, caseId ?? Guid.NewGuid(), CompensationIntent.Template, Ore, "  조사 근거가 확인된 개별 보상입니다.  ");
    private static async Task<string> Csrf(HostFixture host)
    {
        string html = await host.Client.GetStringAsync("/ops");
        return WebUtility.HtmlDecode(Regex.Match(html, "name=\"__RequestVerificationToken\"[^>]*value=\"([^\"]+)\"").Groups[1].Value);
    }
    private static async Task<JsonDocument> Post(HostFixture host, string path, object value, string? csrf, int status, string? code = null)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, path) { Content = JsonContent.Create(value, options:OperationsHttp.Json) };
        if (csrf is not null) request.Headers.Add("X-Ops-CSRF", csrf);
        using var response = await host.Client.SendAsync(request);
        string text = await response.Content.ReadAsStringAsync();
        Check.That((int)response.StatusCode == status, $"Compensation {path} expected {status}, got {(int)response.StatusCode}: {text}");
        var json = JsonDocument.Parse(text);
        if (code is not null) Check.That(json.RootElement.GetProperty("code").GetString() == code, "Expected compensation error " + code + ": " + text);
        return json;
    }
    private static async Task<JsonDocument> Get(HostFixture host, string path)
    {
        using var response = await host.Client.GetAsync(path);
        string text = await response.Content.ReadAsStringAsync();
        Check.That(response.IsSuccessStatusCode, "Compensation read failed: " + text);
        return JsonDocument.Parse(text);
    }
    private static async Task<string> Preview(HostFixture host, CompensationIntent intent, string csrf)
    {
        using var result = await Post(host, "/ops/api/compensations/preview", intent, csrf, 200);
        Check.That(result.RootElement.GetProperty("data").GetProperty("normalizedIntent").GetProperty("reason").GetString() == intent.Reason.Trim(), "Preview normalizes reason before hashing.");
        return result.RootElement.GetProperty("data").GetProperty("previewToken").GetString()!;
    }
    private static async Task<Saved> Verify(HostFixture host)
    {
        await Post(host, "/ops/api/compensations", new { invalid = true }, null, 401);
        await host.LoginAsync(); string csrf = await Csrf(host);
        var services = host.App.Services; var game = services.GetRequiredService<IGameRepository>();
        var items = services.GetRequiredService<IItemRepository>();
        var target = await game.CreateCharacterAsync(HostFixture.Account, "Comp" + Guid.NewGuid().ToString("N")[..10], host.Clock.Now, default);
        var intent = Intent(target.CharacterId);
        await Post(host, "/ops/api/compensations/preview", intent, null, 403, "ops.csrf_failed");
        await Post(host, "/ops/api/compensations/preview", intent with { Reason="short" }, csrf, 400);
        await Post(host, "/ops/api/compensations/preview", intent with { DefinitionKey="arbitrary:stats:1" }, csrf, 403);
        await Post(host, "/ops/api/compensations/preview", new { intent.CharacterId, intent.CaseId, intent.TemplateId, intent.DefinitionKey, intent.Reason, quantity=5 }, csrf, 400);
        await Post(host, "/ops/api/compensations/preview", intent with { Reason=new string('a', 40000) }, csrf, 413);
        string? before = services.GetService<NpgsqlDataSource>() is null ? null : await HostTests.FingerprintAsync(host);
        string token = await Preview(host, intent, csrf);
        if (before is not null) Check.That(before == await HostTests.FingerprintAsync(host), "Preview is strictly read-only.");
        Guid requestId = Guid.NewGuid();
        await Post(host, "/ops/api/compensations", new CompensationSubmission(requestId, token + "bad"), csrf, 400);
        var protector = services.GetRequiredService<CompensationPreviewTokens>();
        var preview = protector.Read(token).Value;
        var otherEnvironment = new CompensationPreviewTokens(services.GetRequiredService<IDataProtectionProvider>(),
            new OperationsOptions { EnvironmentId="another-environment", AllowedGitHubUserId="1" });
        Check.That(!otherEnvironment.Read(token).IsSuccess, "Preview protection is bound to the deployment environment.");
        var wrongActor = protector.Protect(preview with { OperatorId="github:2" });
        await Post(host, "/ops/api/compensations", new CompensationSubmission(requestId, wrongActor), csrf, 400);
        using var committed = await Post(host, "/ops/api/compensations", new CompensationSubmission(requestId, token), csrf, 201);
        var receipt = committed.RootElement.GetProperty("data").GetProperty("receipt");
        Guid itemId = receipt.GetProperty("result").GetProperty("itemId").GetGuid();
        var item = (await items.FindAsync(itemId, default))!;
        Check.That(item.Owner == new ItemOwnerRef(ItemOwnerType.Character, target.CharacterId.ToString("D")) && item.State.Quantity == 1
            && item.Location == new ItemLocation(ItemContainerType.Mail, "OpsCompensation." + requestId.ToString("D"), 0)
            && item.Metadata.CreationSource == CompensationIntent.Template && item.State.GenerationSeed == 0, "Compensation uses server recipe and a single isolated mail attachment even with empty inventory.");
        // A successful same-ID replay precedes expiry/current policy/busy tests.
        host.Clock.Now = host.Clock.Now.AddMinutes(6);
        using var replay = await Post(host, "/ops/api/compensations", new CompensationSubmission(requestId, token), csrf, 200);
        Check.That(replay.RootElement.GetProperty("data").GetProperty("receipt").GetRawText() == receipt.GetRawText(), "Expired preview replays the EXACT original receipt including time and item identity.");
        await Post(host, "/ops/api/compensations", new CompensationSubmission(Guid.NewGuid(), token), csrf, 409, "ops.preview_expired");
        string changed = await Preview(host, intent with { Reason="변경된 근거를 기록하는 다른 입력입니다." }, csrf);
        await Post(host, "/ops/api/compensations", new CompensationSubmission(requestId, changed), csrf, 409, "ops.request_conflict");
        await Post(host, "/ops/api/compensations", new CompensationSubmission(Guid.NewGuid(), changed), csrf, 409, "ops.case_already_compensated");
        var stale = protector.Protect(protector.Read(changed).Value with { PolicyVersion="previous-policy" });
        await Post(host, "/ops/api/compensations", new CompensationSubmission(Guid.NewGuid(), stale), csrf, 409, "ops.compensation_policy_changed");
        string busyToken = await Preview(host, Intent(host.CharacterId), csrf);
        await Post(host, "/ops/api/compensations", new CompensationSubmission(Guid.NewGuid(), busyToken), csrf, 409, "ops.target_busy");
        // Direct store concurrency exercises the atomic boundary without spending the HTTP command rate budget.
        var store = services.GetRequiredService<ICompensationCommitter>();
        var read = services.GetRequiredService<IOperationActionReader>();
        CompensationCommand Command(CompensationIntent value, Guid? id=null) => new(id ?? Guid.NewGuid(),
            preview with { Intent=value.Normalize(), IssuedAtUtc=host.Clock.Now, ExpiresAtUtc=host.Clock.Now.AddMinutes(5) }, Guid.NewGuid(), null);
        var same = Command(Intent(target.CharacterId));
        var outcomes = await Task.WhenAll(Enumerable.Range(0, 6).Select(_ => Task.Run(() => store.CommitAsync(same, default))));
        Check.That(outcomes.All(r => r.IsSuccess) && outcomes.Count(r => r.Value.Disposition=="Committed")==1
            && outcomes.Select(r=>r.Value.Receipt.Result.ItemId).Distinct().Count()==1, "Concurrent same requests grant exactly once.");
        var sameCase = Intent(target.CharacterId);
        var cases = await Task.WhenAll(Enumerable.Range(0, 4).Select(_ => Task.Run(() => store.CommitAsync(Command(sameCase), default))));
        Check.That(cases.Count(r=>r.IsSuccess)==1 && cases.Count(r=>r.Error==CompensationFailures.CaseConflict)==3, "Concurrent different requests cannot repay the same case.");
        using var page1 = await Get(host, $"/ops/api/actions?targetScope=Character&targetId={target.CharacterId:D}&limit=1");
        string cursor = page1.RootElement.GetProperty("data").GetProperty("nextCursor").GetString()!;
        using var page2 = await Get(host, $"/ops/api/actions?targetScope=Character&targetId={target.CharacterId:D}&limit=1&cursor={Uri.EscapeDataString(cursor)}");
        Check.That(page1.RootElement.GetProperty("data").GetProperty("items")[0].GetProperty("requestId").GetGuid()
            != page2.RootElement.GetProperty("data").GetProperty("items")[0].GetProperty("requestId").GetGuid(), "Same-time action pages advance without duplicate rows.");
        Check.That((await host.Client.GetAsync($"/ops/api/actions?targetScope=Character&targetId={host.CharacterId:D}&limit=1&cursor={Uri.EscapeDataString(cursor)}")).StatusCode==HttpStatusCode.BadRequest, "Action cursor cannot be moved to another target.");
        Check.That((await read.SearchAsync(host.CharacterId,new(),default)).Value.Items.Count==0, "Action search isolates target.");
        string html = await host.Client.GetStringAsync($"/ops/characters/{target.CharacterId:D}/compensation");
        Check.That(html.Contains("localStorage.setItem") && html.Contains("sessionStorage.setItem") && html.Contains("X-Ops-CSRF") && !html.Contains("disabled=\"False\""), "Compensation page renders recovery storage and CSRF workflow.");
        // New stored nonterminal session, even with an expired lease, is rejected without cleanup.
        await game.CreateDungeonSessionAsync(HostFixture.Account,target.CharacterId,"training","Normal",host.Clock.Now,host.Clock.Now.AddSeconds(1),default);
        Check.That((await store.CommitAsync(Command(Intent(target.CharacterId)),default)).Error==CompensationFailures.Busy, "Active lease blocks compensation.");
        host.Clock.Now=host.Clock.Now.AddSeconds(2);
        Check.That((await store.CommitAsync(Command(Intent(target.CharacterId)),default)).Error==CompensationFailures.Busy, "Expired nonterminal member still blocks compensation.");
        Check.That((await store.CommitAsync(new(requestId,preview,Guid.NewGuid(),null),default)).IsSuccess, "Busy target still allows original success recovery.");
        await Task.Delay(200);
        using var logs = await Get(host, $"/ops/api/logs?sourceId=backend&operationRequestId={requestId:D}&fromUtc={Uri.EscapeDataString(preview.IssuedAtUtc.AddMinutes(-1).UtcDateTime.ToString("O"))}&toUtc={Uri.EscapeDataString(host.Clock.Now.AddMinutes(1).UtcDateTime.ToString("O"))}");
        Check.That(logs.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Any(e=>e.GetProperty("eventName").GetString()=="ops.command.committed"), "Operation request links to persisted commit log without reason/token leakage.");
        Check.That(!logs.RootElement.GetRawText().Contains(token, StringComparison.Ordinal) && !logs.RootElement.GetRawText().Contains(intent.Reason.Trim(), StringComparison.Ordinal), "Audit reason/preview token never enter technical logs.");
        bool limited=false;
        for(int attempt=0;attempt<31;attempt++)
        {
            using var request=new HttpRequestMessage(HttpMethod.Post,"/ops/api/compensations/preview") { Content=JsonContent.Create(intent,options:OperationsHttp.Json) };
            request.Headers.Add("X-Ops-CSRF",csrf);
            using var response=await host.Client.SendAsync(request);
            if(response.StatusCode==HttpStatusCode.TooManyRequests){limited=true;break;}
            Check.That(response.IsSuccessStatusCode,"Only the configured command rate boundary should reject the read-only preview fixture.");
        }
        Check.That(limited,"Operations commands enforce a per-operator 30/minute budget.");
        return new(requestId,target.CharacterId,itemId,token);
    }
    private static async Task PostgresAtomicity(HostFixture host)
    {
        var services=host.App.Services; var source=services.GetRequiredService<NpgsqlDataSource>();
        var game=services.GetRequiredService<IGameRepository>(); var store=services.GetRequiredService<ICompensationCommitter>();
        var reader=services.GetRequiredService<IOperationActionReader>(); var items=services.GetRequiredService<IItemRepository>();
        var policy=services.GetRequiredService<ICompensationPolicy>();
        var target=await game.CreateCharacterAsync(HostFixture.Account,"Atomic"+Guid.NewGuid().ToString("N")[..9],host.Clock.Now,default);
        CompensationCommand Command(Guid? id=null) => new(id??Guid.NewGuid(),new(Intent(target.CharacterId).Normalize(),"github:1",policy.Version,host.Clock.Now,host.Clock.Now.AddMinutes(5)),Guid.NewGuid(),null);
        var command=Command();
        await using var connection=await source.OpenConnectionAsync();
        // Fail AFTER item effects/receipt but before audit insertion. Every effect must roll back.
        await using(var fault=new NpgsqlCommand("""
            CREATE FUNCTION ops_test_fail_action() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'fixture rollback'; END $$;
            CREATE TRIGGER ops_test_failure BEFORE INSERT ON ops_actions FOR EACH ROW EXECUTE FUNCTION ops_test_fail_action();
            """,connection)) await fault.ExecuteNonQueryAsync();
        try
        {
            Check.That(!(await store.CommitAsync(command,default)).IsSuccess,"Injected audit failure refuses a partial commit.");
            Check.That(await items.TryGetCommitResultAsync(command.RequestId,default) is null && !(await reader.FindAsync(command.RequestId,default)).IsSuccess,
                "Audit failure rolls back item receipt and action.");
            var records=await items.FindByOwnerAsync(new(ItemOwnerType.Character,target.CharacterId.ToString("D")),true,100,default);
            Check.That(records.All(i=>i.Metadata.CreationSource!=CompensationIntent.Template),"Audit failure rolls back the item itself.");
        }
        finally
        {
            await using var remove=new NpgsqlCommand("DROP TRIGGER ops_test_failure ON ops_actions; DROP FUNCTION ops_test_fail_action();",connection);
            await remove.ExecuteNonQueryAsync();
        }
        Check.That((await store.CommitAsync(command,default)).IsSuccess,"Same original ID succeeds after transient audit failure.");
        // Hold a gameplay lease renewal lock while expired to exercise NOWAIT rather than waiting in inverted order.
        var session=await game.CreateDungeonSessionAsync(HostFixture.Account,target.CharacterId,"training","Normal",host.Clock.Now,host.Clock.Now.AddSeconds(1),default);
        host.Clock.Now=host.Clock.Now.AddSeconds(2);
        await using(var tx=await connection.BeginTransactionAsync())
        {
            await using var lease=new NpgsqlCommand("SELECT 1 FROM character_session_leases WHERE character_id=@id FOR UPDATE",connection,tx);
            lease.Parameters.AddWithValue("id",target.CharacterId); await lease.ExecuteNonQueryAsync();
            Check.That((await store.CommitAsync(Command(),default)).Error==CompensationFailures.Busy,"Contended expired lease maps to target_busy, not a deadlock or grant.");
            await tx.RollbackAsync();
        }
        // Gameplay admission explicitly replaces its own expired session; compensation never performs that cleanup.
        session=await game.CreateDungeonSessionAsync(HostFixture.Account,target.CharacterId,"training","Normal",host.Clock.Now,host.Clock.Now.AddHours(1),default);
        await game.AssignDungeonServerAsync(session.DungeonSessionId,"comp-fixture","127.0.0.1:7777",host.Clock.Now,host.Clock.Now.AddHours(1),default);
        await game.StartDungeonSessionAsync(session.DungeonSessionId,"comp-fixture",host.Clock.Now,host.Clock.Now.AddHours(1),default);
        await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,"comp-fixture","fixture",new string('c',64),[],
            [new(FirstSliceEquipmentDefinitions.DefinitionType,FirstSliceEquipmentDefinitions.OreDefinitionName,1,1,ItemBindState.CharacterBound,0,0,[],[],999)],host.Clock.Now,default);
        // Deliberately isolate the pending-settlement guard from membership/lease guards in this owned DB.
        await using(var terminal=new NpgsqlCommand("UPDATE dungeon_sessions SET state='Closed' WHERE dungeon_session_id=@id; UPDATE character_session_leases SET expires_at=@at WHERE dungeon_session_id=@id",connection))
        { terminal.Parameters.AddWithValue("id",session.DungeonSessionId);terminal.Parameters.AddWithValue("at",host.Clock.Now.AddSeconds(-1));await terminal.ExecuteNonQueryAsync(); }
        Check.That((await store.CommitAsync(Command(),default)).Error==CompensationFailures.Busy,"Pending settlement blocks payment independently of terminal parent/expired lease.");
        await using(var processing=new NpgsqlCommand("UPDATE dungeon_reward_settlements SET state='Processing' WHERE dungeon_session_id=@id",connection))
        { processing.Parameters.AddWithValue("id",session.DungeonSessionId);await processing.ExecuteNonQueryAsync(); }
        Check.That((await store.CommitAsync(Command(),default)).Error==CompensationFailures.Busy,"Processing settlement also independently blocks payment.");
        // Expired lease alone does not remove the stored nonterminal membership guard.
        await using(var tx=await connection.BeginTransactionAsync())
        {
            await using var parent=new NpgsqlCommand("SELECT 1 FROM dungeon_sessions WHERE dungeon_session_id=@id FOR UPDATE",connection,tx);
            parent.Parameters.AddWithValue("id",session.DungeonSessionId); await parent.ExecuteNonQueryAsync();
            Check.That((await store.CommitAsync(Command(),default)).Error==CompensationFailures.Busy,"Contended session parent is rejected NOWAIT.");
            await tx.RollbackAsync();
        }
        // Prove the absent-lease admission race closes on character FK locks, without mutating production entry code.
        var entrant=await game.CreateCharacterAsync(HostFixture.Account,"Race"+Guid.NewGuid().ToString("N")[..10],host.Clock.Now,default);
        await using(var tx=await connection.BeginTransactionAsync())
        {
            await using var owner=new NpgsqlCommand("SELECT 1 FROM characters WHERE character_id=@id FOR UPDATE",connection,tx);
            owner.Parameters.AddWithValue("id",entrant.CharacterId); await owner.ExecuteNonQueryAsync();
            var admission=game.CreateDungeonSessionAsync(HostFixture.Account,entrant.CharacterId,"training","Normal",host.Clock.Now,host.Clock.Now.AddMinutes(5),default);
            await Task.Delay(100); Check.That(!admission.IsCompleted,"New lease/member admission waits on the compensation character lock.");
            await tx.RollbackAsync(); await admission;
        }
        // Receipt/audit mismatch is never interpreted as an unsuccessful request that can be reissued.
        await using(var corrupt=new NpgsqlCommand("DELETE FROM item_transaction_receipts WHERE request_id=@id",connection))
        { corrupt.Parameters.AddWithValue("id",command.RequestId); await corrupt.ExecuteNonQueryAsync(); }
        Check.That((await store.CommitAsync(command,default)).Error==CompensationFailures.Inconsistent
            && (await reader.FindAsync(command.RequestId,default)).Error==CompensationFailures.Inconsistent,"Missing gameplay receipt fails closed for replay AND lookup.");
        Console.WriteLine("PASS Compensation PostgreSQL: atomic rollback, case/request concurrency, lease/session NOWAIT, absent-row FK admission barrier, corruption fail-closed and restart recovery.");
    }
}
