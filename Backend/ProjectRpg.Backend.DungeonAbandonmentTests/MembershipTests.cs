using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

static class MembershipTests
{
    private static void Require(bool condition, string message) => Tests.Require(condition, message);
    private static async Task Reject<T>(Func<Task> action) where T : Exception
    {
        try { await action(); } catch (T) { return; }
        throw new TestFailure("Expected membership rejection: " + typeof(T).Name);
    }
    private static async Task<DungeonMembershipSnapshot> Read(Fixture f) =>
        (await f.Repo.GetDungeonMembershipSnapshotAsync(f.Session, f.Server, default))!;
    private static async Task<string> Credential(Fixture f, DateTimeOffset? expires = null)
    {
        string hash = Guid.NewGuid().ToString("N");
        await f.Repo.StoreGameServerCredentialAsync(hash, f.Server, f.Session, expires ?? f.Now.AddHours(2), default);
        return hash;
    }
    private static Task<GameServerCredential?> Resolve(Fixture f, string hash, DateTimeOffset? now = null) =>
        f.Repo.ResolveGameServerCredentialAsync(hash, now ?? f.Now, TimeSpan.FromMinutes(5), default);

    public static async Task RunSuite(string name, Func<IGameRepository> create)
    {
        int passed = 0;
        async Task Test(string label, Func<Fixture, Task> action)
        {
            await action(await Fixture.Create(create()));
            Console.WriteLine($"PASS {name} membership: {label}");
            passed++;
        }
        await Test("consistent active records and immutable abandonment evidence", async f =>
        {
            var before = await Read(f);
            Require(before.Members.Count == 2 && before.Abandonments.Count == 0, "Initial snapshot incorrect.");
            var receipt = await f.Abandon();
            var after = await Read(f);
            Require(after.DungeonSessionId == f.Session && after.ServerId == f.Server
                && after.State == DungeonSessionState.InProgress && after.ExpiresAt == f.Expires
                && after.Members.Single().CharacterId == f.B.CharacterId
                && after.Abandonments.Single() == receipt, "Snapshot lost scoped opt-out evidence.");
            Require(before.Members.Count == 2 && before.Abandonments.Count == 0, "Read view was mutated later.");
        });
        await Test("consumed-ticket race remains visible without restoring authority", async f =>
        {
            string hash = Guid.NewGuid().ToString("N");
            await f.Ticket(hash, f.A);
            Require((await f.Consume(hash))?.CharacterId == f.A.CharacterId, "Initial ticket failed.");
            await f.Abandon();
            Require((await Read(f)).Abandonments.Single().CharacterId == f.A.CharacterId, "Consumed member opt-out invisible.");
            Require(!await f.Repo.IsAuthorizedGameServerSessionMemberAsync(f.Session, f.Server, f.A.SteamId, f.A.CharacterId, f.Now, default), "Snapshot restored admission.");
        });
        await Test("last opt-out retains only restricted read access during grace", async f =>
        {
            string hash = await Credential(f);
            Require(await Resolve(f, hash) is { IsSecurityTelemetryOnly: false }, "Active server restricted too early.");
            await f.Abandon(); await f.Abandon(f.B);
            Require(await Resolve(f, hash) is { IsSecurityTelemetryOnly: true }, "Closed opt-out credential unreadable or unrestricted.");
            Require(await Read(f) is { State: DungeonSessionState.Closed, Members.Count: 0, Abandonments.Count: 2 }, "Last opt-out evidence missing.");
            Require(await Resolve(f, hash, f.Now.AddMinutes(5).AddTicks(10)) is null, "Grace expiry accepted.");
        });
        await Test("credential expiry still wins over abandonment grace", async f =>
        {
            string hash = await Credential(f, f.Now.AddMinutes(1));
            await f.Abandon(); await f.Abandon(f.B);
            Require(await Resolve(f, hash, f.Now.AddMinutes(1)) is null, "Expired bearer accepted during grace.");
        });
        await Test("natural expiry is not explicit abandonment or extended access", async f =>
        {
            string hash = await Credential(f);
            await f.Repo.GetDungeonSessionAsync(f.Session, f.Expires, default);
            Require(await Resolve(f, hash, f.Expires) is null, "Natural expiry gained opt-out grace.");
            Require(await Read(f) is { State: DungeonSessionState.Closed, Members.Count: 2, Abandonments.Count: 0 }, "Expiry manufactured receipts.");
        });
        await Test("partial opt-out followed by expiry does not grant closed grace", async f =>
        {
            string hash = await Credential(f); await f.Abandon();
            await f.Repo.GetDungeonSessionAsync(f.Session, f.Expires, default);
            Require(await Resolve(f, hash, f.Expires) is null, "Partial receipt gave natural expiry closed grace.");
            Require((await Read(f)).Abandonments.Single().SessionStateAtAbandonment == DungeonSessionState.InProgress, "Historical receipt changed.");
        });
        await Test("settlement and terminal snapshots preserve opt-out receipts", async f =>
        {
            string hash = await Credential(f); var receipt = await f.Abandon();
            await f.Settle();
            Require(await Resolve(f, hash) is { IsSecurityTelemetryOnly: true }, "Pending bearer not restricted.");
            Require((await Read(f)).Abandonments.Single() == receipt, "Pending receipt lost.");
            var claimed = await f.Repo.ClaimNextDungeonRewardSettlementAsync(f.Server, f.Now, f.Expires, default);
            Require(claimed?.DungeonSessionId == f.Session, "Membership fixture claimed another settlement.");
            await f.Repo.CompleteDungeonRewardSettlementAsync(f.Session, f.Server, f.Now, default);
            var cleared = await Read(f);
            Require(cleared.State == DungeonSessionState.Cleared && cleared.Abandonments.Single() == receipt
                && cleared.Members.Single().CharacterId == f.B.CharacterId, "Terminal snapshot changed rewards or membership.");
        });
        await Test("read-only snapshot cannot alter a newer character lease", async f =>
        {
            await f.Abandon();
            var next = await f.Repo.CreateDungeonSessionAsync(f.A.SteamId, f.A.CharacterId, "Dungeon.Next", "Hard", f.Now, f.Expires, default);
            await Read(f); await Read(f);
            Require((await f.Repo.GetActiveDungeonSessionForCharacterAsync(f.A.CharacterId, f.A.SteamId, f.Now, default))?.DungeonSessionId == next.DungeonSessionId, "Snapshot released new lease.");
        });
        await Test("wrong assignment unknown session and cancellation are not opt-out evidence", async f =>
        {
            await Reject<DungeonSessionServerMismatchException>(() => f.Repo.GetDungeonMembershipSnapshotAsync(f.Session, f.Server + "-wrong", default));
            Require(await f.Repo.GetDungeonMembershipSnapshotAsync(Guid.NewGuid(), f.Server, default) is null, "Unknown snapshot fabricated.");
            using CancellationTokenSource cancel = new(); cancel.Cancel();
            await Reject<OperationCanceledException>(() => f.Repo.GetDungeonMembershipSnapshotAsync(f.Session, f.Server, cancel.Token));
            Require((await Read(f)).Members.Count == 2, "Rejected read changed members.");
        });
        await Test("concurrent reads never mix member removal and receipt commit", async f =>
        {
            for (int index = 0; index < 20; index++)
            {
                Fixture race = await Fixture.Create(create());
                var abandon = Task.Run(() => race.Abandon());
                var snapshots = await Task.WhenAll(Enumerable.Range(0, 4).Select(_ => Task.Run(() => Read(race))));
                await abandon;
                foreach (var snapshot in snapshots)
                    Require(snapshot.Members.Count + snapshot.Abandonments.Count == 2
                        && !snapshot.Members.Any(member => snapshot.Abandonments.Any(receipt => receipt.CharacterId == member.CharacterId)), "Read crossed an abandonment commit.");
            }
        });
        await Test("real HTTP scoped server GET and restricted post-close method/path boundaries", CheckHttp);
        Console.WriteLine($"PASS {name} membership suite: {passed} scenarios (including 20 read races).");
    }

    private static async Task CheckHttp(Fixture f)
    {
        MembershipClock clock = new(f.Now);
        AccessTokenService tokens = new(clock);
        var server = tokens.Issue(TimeSpan.FromHours(2)); var player = tokens.Issue(TimeSpan.FromHours(2));
        string admin = tokens.Issue(TimeSpan.FromHours(2)).Token;
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo); builder.Services.AddSingleton<TimeProvider>(clock); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapDungeonMembershipSnapshotApi();
        // Diagnostic success routes make an accidental middleware privilege expansion fail.
        app.MapMethods("/api/dungeon-sessions/{id:guid}/heartbeat", ["POST"], () => Results.Ok());
        app.MapGet("/api/characters/{id:guid}/inventory", () => Results.Ok());
        await app.StartAsync();
        using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
        string path = $"/api/dungeon-sessions/{f.Session:D}/membership-snapshot";
        async Task<HttpResponseMessage> Send(string? token, HttpMethod? method = null, string? target = null)
        {
            using HttpRequestMessage request = new(method ?? HttpMethod.Get, target ?? path);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            return await client.SendAsync(request);
        }
        foreach (var (token, status) in new[] { ((string?)null, 401), ("invalid", 401), (player.Token, 403), (admin, 403) })
        {
            using var response = await Send(token);
            Require((int)response.StatusCode == status, "Membership HTTP principal boundary failed.");
        }
        using var initial = await Send(server.Token);
        Require(initial.StatusCode == HttpStatusCode.OK && initial.Headers.CacheControl?.NoStore == true, "Assigned server snapshot unavailable or cached.");
        var active = await initial.Content.ReadFromJsonAsync<DungeonMembershipSnapshotResponse>();
        Require(active is { Members.Count: 2, Abandonments.Count: 0 } && active.ServerId == f.Server, "HTTP active snapshot malformed.");
        using var foreignSession = await Send(server.Token, target: path.Replace(f.Session.ToString("D"), Guid.NewGuid().ToString("D")));
        Require(foreignSession.StatusCode == HttpStatusCode.Forbidden, "Active server read another session.");
        await f.Abandon();
        using var partial = await Send(server.Token);
        var partialSnapshot = await partial.Content.ReadFromJsonAsync<DungeonMembershipSnapshotResponse>();
        Require(partial.StatusCode == HttpStatusCode.OK && partialSnapshot is { Members.Count: 1, Abandonments.Count: 1 }
            && partialSnapshot.Members.Single().CharacterId == f.B.CharacterId, "Partial opt-out changed the remaining HTTP party.");
        await f.Abandon(f.B);
        using var closed = await Send(server.Token);
        var snapshot = await closed.Content.ReadFromJsonAsync<DungeonMembershipSnapshotResponse>();
        Require(closed.StatusCode == HttpStatusCode.OK && snapshot is { State: "Closed", Members.Count: 0, Abandonments.Count: 2 }
            && snapshot.DungeonSessionId == f.Session && snapshot.Abandonments.All(receipt => receipt.RewardExcluded)
            && snapshot.Abandonments.Any(receipt => receipt.CharacterId == f.A.CharacterId && receipt.SteamId == f.A.SteamId), "Closed server cannot read exact opt-out evidence.");
        foreach (var (method, target) in new[] {
            (HttpMethod.Post, path), (HttpMethod.Delete, path), (HttpMethod.Head, path),
            (HttpMethod.Get, path + "/"), (HttpMethod.Get, path + "/extra"),
            (HttpMethod.Get, path.Replace(f.Session.ToString("D"), Guid.NewGuid().ToString("D"))),
            (HttpMethod.Post, $"/api/dungeon-sessions/{f.Session:D}/heartbeat"),
            (HttpMethod.Get, $"/api/characters/{f.A.CharacterId:D}/inventory") })
        {
            using var rejected = await Send(server.Token, method, target);
            Require(rejected.StatusCode == HttpStatusCode.Forbidden, "Restricted credential gained another method, scope or privilege.");
        }
        clock.Now = f.Now.AddMinutes(5).AddTicks(10);
        using var expired = await Send(server.Token);
        Require(expired.StatusCode == HttpStatusCode.Unauthorized, "HTTP closed grace expiry accepted.");
        await app.StopAsync();
        AuthenticatedPrincipal unscoped = new(PrincipalKind.GameServer, null, f.Server);
        Require(!DungeonMembershipSnapshotAccess.IsOwnReadOnlyRoute("GET", path, unscoped), "Unscoped server allowed restricted GET.");
    }
}

sealed class MembershipClock(DateTimeOffset now) : TimeProvider
{
    public DateTimeOffset Now { get; set; } = now;
    public override DateTimeOffset GetUtcNow() => Now;
}
