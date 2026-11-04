using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Reflection;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Tests;

static class JoinTicketIssuanceTests
{
    private sealed class Clock : TimeProvider
    {
        private long ticks = DateTimeOffset.UtcNow.UtcTicks;
        public void Set(DateTimeOffset value) => Interlocked.Exchange(ref ticks, value.UtcTicks);
        public override DateTimeOffset GetUtcNow() => new(Interlocked.Read(ref ticks), TimeSpan.Zero);
    }

    private static Task<JoinTicketIssueResult> Issue(Fixture f, string hash, GameCharacter? member = null)
        => f.Repo.StoreJoinTicketAsync(hash, (member ?? f.A).SteamId, (member ?? f.A).CharacterId,
            f.Session, f.Expires, default);

    private static async Task Disconnect(Fixture f)
    {
        ConsumedJoinTicket admission = await f.Admit();
        await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId,
            f.A.CharacterId, admission.ConnectionGeneration, f.Now, default);
    }

    public static async Task RunSuite(string provider, Func<TimeProvider, IGameRepository> create)
    {
        async Task Test(string name, Func<Fixture, Clock, Task> action)
        {
            Clock clock = new(); Fixture f = await Fixture.Create(create(clock)); clock.Set(f.Now);
            await action(f, clock); Console.WriteLine($"PASS {provider} ticket issuance: {name}");
        }

        await Test("exact deadline rejects without writing a ticket or changing the party", async (f, clock) =>
        {
            await Disconnect(f); DungeonSession before = await f.Read();
            clock.Set(f.Now.AddMinutes(10).AddTicks(-10));
            Require(await Issue(f, "before-deadline") == JoinTicketIssueResult.Issued, "In-grace issuance rejected.");
            clock.Set(f.Now.AddMinutes(10));
            Require(await Issue(f, "at-deadline") == JoinTicketIssueResult.ReconnectGraceExpired, "Late ticket issued.");
            Require((await f.Read()).Members.SequenceEqual(before.Members), "Issuance mutated membership/generation.");
            Require(await Issue(f, "remaining-member", f.B) == JoinTicketIssueResult.Issued, "Remaining party blocked.");
            clock.Set(f.Now.AddMinutes(9));
            Require(await f.Consume("at-deadline") is null, "Rejected ticket was persisted.");
            Require(await f.Consume("before-deadline") is not null, "Denial removed an older valid ticket.");
        });
        await Test("confirmed recovery clears expiry but ticket issuance alone does not", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
            ConsumedJoinTicket next = await f.Admit();
            Require(await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, next.ConnectionGeneration, f.Now, default), "Recovery failed.");
            clock.Set(f.Now.AddMinutes(20));
            Require(await Issue(f, "recovered") == JoinTicketIssueResult.Issued, "Recovered member blocked.");
        });
        await Test("exact identity and active session required without leaking another account's expiry", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(10));
            Require(await f.Repo.StoreJoinTicketAsync("foreign", f.B.SteamId, f.A.CharacterId, f.Session,
                f.Expires, default) == JoinTicketIssueResult.NotActiveMember, "Foreign account observed expiry.");
            Require(await Issue(f, "nonmember", f.C) == JoinTicketIssueResult.NotActiveMember, "Nonmember admitted.");
            Fixture waiting = await Fixture.Create(f.Repo, DungeonSessionState.Waiting);
            Require(await Issue(waiting, "waiting") == JoinTicketIssueResult.NotActiveMember, "Waiting session admitted.");
            Fixture loading = await Fixture.Create(f.Repo, DungeonSessionState.Loading);
            Require(await Issue(loading, "loading") == JoinTicketIssueResult.Issued, "Ready loading session blocked.");
            Require(await f.Repo.StoreJoinTicketAsync("expired-token", f.B.SteamId, f.B.CharacterId,
                f.Session, f.Now, default) == JoinTicketIssueResult.NotActiveMember, "Expired token persisted.");
        });
        await Test("immutable expiry stays distinct from explicit abandonment after member removal", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(10));
            await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default);
            await f.Abandon(f.B);
            Require((await f.Read()).State == DungeonSessionState.Closed, "Fixture did not close.");
            Require(await Issue(f, "expelled") == JoinTicketIssueResult.ReconnectGraceExpired, "Expiry receipt lost.");
            Require(await Issue(f, "abandoned", f.B) == JoinTicketIssueResult.NotActiveMember, "Abandonment inferred expiry.");
            Require(await f.Repo.StoreJoinTicketAsync("foreign-receipt", f.B.SteamId, f.A.CharacterId,
                f.Session, f.Expires, default) == JoinTicketIssueResult.NotActiveMember, "Foreign receipt leaked.");
        });
        await Test("real player HTTP denies expired travel and keeps the other member playable", CheckHttp);
    }

    private static async Task CheckHttp(Fixture f, Clock clock)
    {
        await Disconnect(f);
        AccessTokenService tokens = new(clock);
        var player = tokens.Issue(TimeSpan.FromHours(1)); var other = tokens.Issue(TimeSpan.FromHours(1));
        var server = tokens.Issue(TimeSpan.FromHours(1));
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        await f.Repo.StoreSessionAsync(other.TokenHash, f.B.SteamId, other.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo); builder.Services.AddSingleton<TimeProvider>(clock);
        builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        StoryAdmissionTestServices.AddUnrestricted(builder.Services);
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapJoinTicketApi();
        await app.StartAsync();
        try
        {
            using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
            async Task<HttpResponseMessage> Send(string? token, GameCharacter member)
            {
                using HttpRequestMessage request = new(HttpMethod.Post, "/api/join-tickets");
                if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
                request.Content = JsonContent.Create(new CreateJoinTicketRequest(member.CharacterId, f.Session));
                return await client.SendAsync(request);
            }
            using (var denied = await Send(null, f.A)) Require(denied.StatusCode == HttpStatusCode.Unauthorized, "Anonymous issue.");
            using (var denied = await Send(server.Token, f.A)) Require(denied.StatusCode == HttpStatusCode.Forbidden, "Server issue.");
            clock.Set(f.Now.AddMinutes(10));
            using (var denied = await Send(player.Token, f.A))
            {
                string body = await denied.Content.ReadAsStringAsync();
                Require(denied.StatusCode == HttpStatusCode.Conflict && body.Contains("dungeon_reconnect_grace_expired")
                    && !body.Contains("joinTicket") && !body.Contains("expiresAt"), "Missing safe expiry response.");
            }
            using (var denied = await Send(other.Token, f.A))
                Require(denied.StatusCode == HttpStatusCode.Conflict
                    && (await denied.Content.ReadAsStringAsync()).Contains("dungeon_member_not_active"), "Foreign expiry leaked.");
            using (var accepted = await Send(other.Token, f.B))
            {
                CreateJoinTicketResponse? ticket = await accepted.Content.ReadFromJsonAsync<CreateJoinTicketResponse>();
                Require(accepted.StatusCode == HttpStatusCode.OK && ticket?.CharacterId == f.B.CharacterId
                    && ticket.DungeonSessionId == f.Session
                    && await f.Consume(tokens.Hash(ticket.JoinTicket)) is not null, "Remaining member admission failed.");
            }
        }
        finally { await app.StopAsync(); }
    }

    public static async Task CheckMemoryLock()
    {
        Clock clock = new(); InMemoryGameRepository repo = new(clock);
        Fixture f = await Fixture.Create(repo); await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
        object gate = typeof(InMemoryGameRepository).GetField("_dungeonGate", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(repo)!;
        Task<JoinTicketIssueResult> pending; Thread? waiting = null;
        lock (gate)
        {
            pending = Task.Run(async () => { waiting = Thread.CurrentThread; return await Issue(f, "lock-delayed"); });
            Require(SpinWait.SpinUntil(() => waiting is not null
                && (waiting.ThreadState & System.Threading.ThreadState.WaitSleepJoin) != 0, TimeSpan.FromSeconds(5)), "No gate wait.");
            clock.Set(f.Now.AddMinutes(10));
        }
        Require(await pending.WaitAsync(TimeSpan.FromSeconds(10)) == JoinTicketIssueResult.ReconnectGraceExpired, "Pre-lock clock bypass.");
        Console.WriteLine("PASS InMemory ticket issuance: clock refreshed after lock wait");
    }

    public static async Task CheckPostgresLock(PrivateSchema schema)
    {
        Clock clock = new(); Fixture f = await Fixture.Create(schema.Repository(clock));
        await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
        await using NpgsqlConnection connection = await schema.Source.OpenConnectionAsync();
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync();
        await using (NpgsqlCommand hold = new("SELECT 1 FROM dungeon_sessions WHERE dungeon_session_id = @id FOR UPDATE", connection, transaction))
        { hold.Parameters.AddWithValue("id", f.Session); await hold.ExecuteNonQueryAsync(); }
        Task<JoinTicketIssueResult> pending = Issue(f, "row-lock-delayed");
        bool released = false;
        try
        {
            string application = new NpgsqlConnectionStringBuilder(schema.Source.ConnectionString).ApplicationName!;
            bool observed = false; DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                await using var probe = schema.Source.CreateCommand("SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE application_name=@application AND wait_event_type='Lock' AND query LIKE '%FOR UPDATE%');");
                probe.Parameters.AddWithValue("application", application);
                if (await probe.ExecuteScalarAsync() is true) { observed = true; break; }
                await Task.Delay(10);
            }
            Require(observed && !pending.IsCompleted, "Issuance did not wait for the parent row lock.");
            clock.Set(f.Now.AddMinutes(10)); await transaction.CommitAsync(); released = true;
            Require(await pending.WaitAsync(TimeSpan.FromSeconds(10)) == JoinTicketIssueResult.ReconnectGraceExpired, "PG pre-lock clock bypass.");
        }
        finally
        {
            if (!released) await transaction.RollbackAsync();
            await pending.WaitAsync(TimeSpan.FromSeconds(10));
        }
        Console.WriteLine("PASS Postgres ticket issuance: clock refreshed after row lock wait");
    }
}
