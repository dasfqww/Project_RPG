using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Npgsql;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Authentication;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;
using ProjectRpg.Backend.Data;

internal static class HostTests
{
    internal static async Task RunAsync()
    {
        string root=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")
            ?? throw new InvalidOperationException("Use the owned Operations test runner.");
        await LogReaderTests.RunAsync(Path.Combine(root,"log-reader"));
        await using (var disabled=await HostFixture.StartAsync(Path.Combine(root,"disabled"),enabled:false))
        {
            Check.That((await disabled.Client.GetAsync("/ops")).StatusCode==HttpStatusCode.NotFound,"Disabled operations has no public route.");
        }
        await using (var memory=await HostFixture.StartAsync(Path.Combine(root,"memory")))
        {
            await memory.SeedAsync();
            await SessionAbsoluteExpiryAsync(memory);
            await AccessAndReadAsync(memory);
            await EvidenceHostTests.RunAsync(memory);
            await CursorAndSessionAsync(memory);
        }
        string? postgres=Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES");
        if (postgres is not null)
        {
            var connection=new NpgsqlConnectionStringBuilder(postgres);
            Check.That(connection.Host=="127.0.0.1" && connection.Database=="ops_test" && connection.Username=="ops_test"
                && Guid.TryParse(Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_RUN"),out _),"Owned PostgreSQL only.");
            string pgRoot=Path.Combine(root,"postgres");
            await using (var source=NpgsqlDataSource.Create(postgres))
            {
                var catalog=await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory,default);
                Check.That(await PostgresSchemaMigrations.ApplyAsync(source,catalog.Take(4).ToArray(),default)==4,"Published v4 catalog installs.");
                Check.That(await PostgresSchemaMigrations.ApplyAsync(source,catalog,default)==1,"Operations upgrades v4 with exactly one append migration.");
                Check.That(await PostgresSchemaMigrations.ApplyAsync(source,catalog,default)==0,"Operations migration is idempotent through history.");
                await PostgresSchemaMigrations.ValidateAsync(source,catalog,default);
            }
            var pg=await HostFixture.StartAsync(pgRoot,postgres);
            try
            {
                await pg.SeedAsync();
                await SessionAbsoluteExpiryAsync(pg);
                string before=await FingerprintAsync(pg);
                await AccessAndReadAsync(pg);
                Check.That(before==await FingerprintAsync(pg),"Operations reads leave every gameplay table unchanged (including expired leases/progression).");
                await EvidenceHostTests.RunAsync(pg);
                CookieContainer cookies=pg.Cookies; int port=pg.Port; MutableClock clock=pg.Clock; Guid id=pg.CharacterId;
                await pg.StopAsync();
                await using (var restored=await HostFixture.StartAsync(pgRoot,postgres,port,cookies,clock))
                {
                    Check.That((await restored.Client.GetAsync("/ops/api/characters/"+id)).IsSuccessStatusCode,"PG server ticket and protected cookie survive host restart.");
                }
                await using (var rotated=await HostFixture.StartAsync(pgRoot,postgres,port,cookies,clock,"operations-test-epoch-0002"))
                    Check.That((await rotated.Client.GetAsync("/ops/api/characters/"+id)).StatusCode==HttpStatusCode.Unauthorized,"AuthEpoch rotation invalidates restored sessions.");
                Console.WriteLine("PASS PostgreSQL: gameplay unchanged, session persistence, key persistence, epoch revocation.");
            }
            finally { await pg.DisposeAsync(); }
        }
    }

    private static async Task AccessAndReadAsync(HostFixture host)
    {
        using (var invalid=await host.Client.GetAsync("/ops/api/characters?unknown=secret-input-not-for-logs"))
            Check.That(invalid.StatusCode==HttpStatusCode.Unauthorized,"Anonymous invalid query is rejected before validation.");
        host.Client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer","fixture-admin-token");
        Check.That((await host.Client.GetAsync("/api/protected")).IsSuccessStatusCode,"Existing game admin bearer still works.");
        Check.That((await host.Client.GetAsync("/ops/api/characters/"+host.CharacterId)).StatusCode==HttpStatusCode.Unauthorized,"Game admin bearer is not an operator cookie.");
        host.Client.DefaultRequestHeaders.Authorization=null;
        Check.That((await host.Client.GetAsync("/ops/auth/github-callback?code=invalid&state=invalid")).StatusCode==HttpStatusCode.Forbidden,"Invalid OAuth state fails closed.");
        Check.That((await host.Client.PostAsync("/ops/auth/login",new FormUrlEncodedContent(new Dictionary<string,string>()))).StatusCode==HttpStatusCode.Forbidden,"Login POST requires CSRF.");
        string csrf=await host.CsrfAsync();
        host.GitHub.UserId=2;
        using (var denied=await host.LoginAsync(csrf))
            Check.That(denied.StatusCode==HttpStatusCode.Forbidden,"Unlisted numeric GitHub identity denied.");
        host.GitHub.UserId=1;
        host.GitHub.Unavailable=true;
        using (var unavailable=await host.LoginAsync(csrf))
            Check.That(unavailable.StatusCode==HttpStatusCode.ServiceUnavailable,"Provider outage is distinguished from access denial.");
        host.GitHub.Unavailable=false;
        using (var login=await host.LoginAsync(csrf))
        {
            Check.That(login.StatusCode==HttpStatusCode.Redirect && host.GitHub.SawVerifier,"Full OAuth callback verifies PKCE and issues session.");
            Check.That(login.Headers.GetValues("Set-Cookie").Any(c=>c.StartsWith("__Host-ProjectRpgOps=",StringComparison.Ordinal)
                && c.Contains("secure",StringComparison.OrdinalIgnoreCase) && c.Contains("httponly",StringComparison.OrdinalIgnoreCase)),"Session cookie is Secure and HttpOnly.");
        }
        Check.That((await host.Client.GetAsync("/api/protected")).StatusCode==HttpStatusCode.Unauthorized,"Operations cookie does not grant game API access.");
        using var first=await JsonAsync(host,"/ops/api/characters?kind=SteamId&value="+HostFixture.Account+"&limit=1");
        Check.That(first.RootElement.GetProperty("data").GetProperty("items").GetArrayLength()==1,"Bound character page.");
        string cursor=first.RootElement.GetProperty("data").GetProperty("nextCursor").GetString()!;
        using var second=await JsonAsync(host,"/ops/api/characters?kind=SteamId&value="+HostFixture.Account+"&limit=1&cursor="+Uri.EscapeDataString(cursor));
        Check.That(second.RootElement.GetProperty("data").GetProperty("items")[0].GetProperty("characterId").GetGuid()
            != first.RootElement.GetProperty("data").GetProperty("items")[0].GetProperty("characterId").GetGuid(),"Keyset next page does not duplicate first row.");
        Check.That((await host.Client.GetAsync("/ops/api/characters?kind=SteamId&value=01")).StatusCode==HttpStatusCode.BadRequest,"Canonical account ID enforced.");
        Check.That((await host.Client.GetAsync("/ops/api/characters/"+Guid.NewGuid())).StatusCode==HttpStatusCode.NotFound,"Missing target is 404.");
        using var wallet=await JsonAsync(host,"/ops/api/characters/"+host.CharacterId+"/wallet");
        var coin=wallet.RootElement.GetProperty("data").GetProperty("balances").EnumerateArray().Single(x=>x.GetProperty("currencyCode").GetString()=="OPS_COIN");
        Check.That(coin.GetProperty("balance").ValueKind==JsonValueKind.String&&coin.GetProperty("balance").GetString()=="9007199254740993","Int64 is lossless JSON string.");
        using var inventory=await JsonAsync(host,"/ops/api/characters/"+host.CharacterId+"/inventory?container=Equipment&limit=1");
        Check.That(inventory.RootElement.GetProperty("data").GetProperty("items").GetArrayLength()==1,"Inventory uses stored starter items and bounded page.");
        using var sessions=await JsonAsync(host,"/ops/api/characters/"+host.CharacterId+"/sessions");
        JsonElement session=sessions.RootElement.GetProperty("data").GetProperty("items")[0];
        Check.That(session.GetProperty("storedState").GetString()=="Waiting" && session.GetProperty("timing").GetString()=="ObservedExpired","Expired stored session remains Waiting, with observation only.");
        using var detailResponse=await host.Client.GetAsync("/ops/characters/"+host.CharacterId+"?panel=inventory&container=Equipment");
        string html=await detailResponse.Content.ReadAsStringAsync();
        Check.That(detailResponse.IsSuccessStatusCode && html.Contains("Ops") && html.Contains(host.CharacterId.ToString()),"Compiled Razor character panel is reachable.");
        string requestId=detailResponse.Headers.GetValues("X-Ops-Request-Id").Single();
        await Task.Delay(500);
        string from=Uri.EscapeDataString(host.Clock.Now.AddHours(-1).UtcDateTime.ToString("O"));
        string to=Uri.EscapeDataString(host.Clock.Now.AddMinutes(1).UtcDateTime.ToString("O"));
        using var logs=await JsonAsync(host,$"/ops/api/logs?httpRequestId={requestId}&fromUtc={from}&toUtc={to}");
        if (logs.RootElement.GetProperty("data").GetProperty("events").GetArrayLength()<2)
        {
            Console.WriteLine("Log search failure: "+logs.RootElement.GetRawText());
            foreach(string file in Directory.GetFiles(Path.Combine(host.Root,"logs"),"*.jsonl"))
            {
                using var stream=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
                using var reader=new StreamReader(stream);
                Console.WriteLine("Typed sink sample: "+await reader.ReadLineAsync());
            }
            Console.WriteLine("Sink errors: "+host.App.Services.GetRequiredService<OperationsLogStore>().SinkErrors);
        }
        Check.That(logs.RootElement.GetProperty("data").GetProperty("events").GetArrayLength()>=2,"Trace number finds completed request and query events in persisted JSONL.");
        var traced=logs.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().ToArray();
        Check.That(traced.All(e=>e.GetProperty("traceId").GetString()?.Length==32),"Every recorded request has a trace even without an external telemetry listener.");
        if(host.App.Services.GetService<NpgsqlDataSource>() is not null)
            Check.That(traced.Any(e=>e.GetProperty("eventName").GetString()=="ops.storage.query.completed"
                && e.GetProperty("parentSpanId").ValueKind==JsonValueKind.String),"PG storage child spans link to the HTTP trace.");
        Check.That(host.App.Services.GetRequiredService<OperationsOptions>().Enabled,"Fixture exercised enabled production module.");
        foreach(string file in Directory.GetFiles(Path.Combine(host.Root,"logs"),"*.jsonl"))
        {
            using var stream=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
            using var reader=new StreamReader(stream);
            string contents=await reader.ReadToEndAsync();
            Check.That(!contents.Contains("fixture-oauth-token-never-log") && !contents.Contains("fixture-admin-token")
                && !contents.Contains("secret-input-not-for-logs") && !contents.Contains("__RequestVerificationToken"),"No credential/body/query dump in log sink.");
        }
        Console.WriteLine("PASS HTTP: OAuth/PKCE/CSRF/allowlist, game authority isolation, paging/int64/lease evidence, Razor and persisted log trace.");
    }

    private static async Task CursorAndSessionAsync(HostFixture host)
    {
        var cursors=host.App.Services.GetRequiredService<OperationsCursor>();
        var state=cursors.Read<PagePosition>(null,"github:1","test").Value;
        string token=cursors.Write(new PagePosition(Guid.NewGuid()),state,"github:1","test")!;
        Check.That(!cursors.Read<PagePosition>(token,"github:2","test").IsSuccess
            && !cursors.Read<PagePosition>(token,"github:1","other-query").IsSuccess
            && !cursors.Read<PagePosition>(token+"x","github:1","test").IsSuccess,"Cursor binds identity/filter and rejects tampering.");
        host.Clock.Now=host.Clock.Now.AddMinutes(16);
        Check.That(cursors.Read<PagePosition>(token,"github:1","test").Error==Failures.CursorExpired,"Cursor expires without sliding.");
        host.Clock.Now=host.Clock.Now.AddMinutes(15);
        Check.That((await host.Client.GetAsync("/ops/api/characters/"+host.CharacterId)).StatusCode==HttpStatusCode.Unauthorized,"Idle session expires on server.");
        using (var login=await host.LoginAsync()) Check.That(login.StatusCode==HttpStatusCode.Redirect,"Re-login after idle expiry.");
        Check.That((await host.Client.PostAsync("/ops/auth/logout",new FormUrlEncodedContent(new Dictionary<string,string>()))).StatusCode==HttpStatusCode.Forbidden,"Logout requires CSRF.");
        // A fresh form token is tied to the now-authenticated identity.
        string html=await host.Client.GetStringAsync("/ops");
        var match=System.Text.RegularExpressions.Regex.Match(html,"name=\"__RequestVerificationToken\"[^>]*value=\"([^\"]+)\"");
        CookieContainer oldCookies=new();
        foreach(Cookie cookie in host.Cookies.GetCookies(new Uri(host.Origin))) oldCookies.Add(new Cookie(cookie.Name,cookie.Value,cookie.Path,cookie.Domain){Secure=cookie.Secure});
        using var logout=await host.Client.PostAsync("/ops/auth/logout",new FormUrlEncodedContent(new Dictionary<string,string>
            { ["__RequestVerificationToken"]=WebUtility.HtmlDecode(match.Groups[1].Value) }));
        Check.That(logout.StatusCode==HttpStatusCode.Redirect,"Logout commits server revocation.");
        using HttpClient replay=new(new HttpClientHandler { CookieContainer=oldCookies,AllowAutoRedirect=false,
            ServerCertificateCustomValidationCallback=HttpClientHandler.DangerousAcceptAnyServerCertificateValidator }) { BaseAddress=new Uri(host.Origin) };
        Check.That((await replay.GetAsync("/ops/api/characters/"+host.CharacterId)).StatusCode==HttpStatusCode.Unauthorized,"Old cookie cannot survive logout revocation.");
        Console.WriteLine("PASS sessions/cursors: identity/filter binding, tamper/expiry, idle expiry, logout and revoked-cookie replay.");
    }

    private static async Task<JsonDocument> JsonAsync(HostFixture fixture,string path)
    {
        using var response=await fixture.Client.GetAsync(path);
        string body=await response.Content.ReadAsStringAsync();
        Check.That(response.IsSuccessStatusCode,"Expected read success for "+path.Split('?')[0]+"; got "+(int)response.StatusCode+" "+body);
        Check.That(response.Headers.CacheControl?.NoStore==true,"Operations response is not cacheable.");
        return JsonDocument.Parse(body);
    }
    internal static async Task<string> FingerprintAsync(HostFixture host)
    {
        var source=host.App.Services.GetRequiredService<NpgsqlDataSource>();
        await using var connection=await source.OpenConnectionAsync();
        List<string> tables=[];
        await using(var list=new NpgsqlCommand("SELECT tablename FROM pg_tables WHERE schemaname='public' AND tablename NOT LIKE 'ops_%' ORDER BY tablename;",connection))
        await using(var rows=await list.ExecuteReaderAsync()) while(await rows.ReadAsync()) tables.Add(rows.GetString(0));
        List<string> hashes=[];
        foreach(string table in tables)
        {
            string quoted=new NpgsqlCommandBuilder().QuoteIdentifier(table);
            await using var command=new NpgsqlCommand($"SELECT COALESCE(md5(string_agg(r::text,',' ORDER BY r::text)),'empty') FROM (SELECT to_jsonb(t) r FROM {quoted} t) s;",connection);
            hashes.Add(table+":"+(string)(await command.ExecuteScalarAsync())!);
        }
        return string.Join("\n",hashes);
    }

    private static async Task SessionAbsoluteExpiryAsync(HostFixture host)
    {
        var repository=host.App.Services.GetRequiredService<IOperatorSessionRepository>();
        var settings=host.App.Services.GetRequiredService<OperationsOptions>();
        byte[] hash=System.Security.Cryptography.RandomNumberGenerator.GetBytes(32);
        DateTimeOffset now=host.Clock.Now;
        await repository.StoreAsync(new(hash,settings.OperatorId,[1],settings.AuthEpoch,now,now,now.AddHours(8)),default);
        for(int minute=20;minute<480;minute+=20)
            Check.That(await repository.TouchAsync(hash,settings.OperatorId,settings.AuthEpoch,now.AddMinutes(minute),default) is not null,"Active session touches preserve its absolute deadline.");
        Check.That(await repository.TouchAsync(hash,settings.OperatorId,settings.AuthEpoch,now.AddHours(8),default) is null,"Absolute eight-hour deadline expires even an active session.");
        await repository.RevokeAsync(hash,now.AddHours(8),default);
    }
}
