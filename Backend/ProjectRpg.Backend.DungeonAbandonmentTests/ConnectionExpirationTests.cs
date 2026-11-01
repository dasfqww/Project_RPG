using System.Reflection;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
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

static class ConnectionExpirationTests
{
    private sealed class Clock : TimeProvider
    {
        private long ticks = DateTimeOffset.UtcNow.UtcTicks;
        public void Set(DateTimeOffset now) => Interlocked.Exchange(ref ticks, now.UtcTicks);
        public override DateTimeOffset GetUtcNow() => new(Interlocked.Read(ref ticks), TimeSpan.Zero);
    }

    private static async Task<ConsumedJoinTicket> Disconnect(Fixture f, GameCharacter? member = null)
    {
        member ??= f.A;
        ConsumedJoinTicket admission = await f.Admit(member);
        await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, member.SteamId,
            member.CharacterId, admission.ConnectionGeneration, f.Now, default);
        return admission;
    }

    public static async Task RunSuite(string provider, Func<TimeProvider, IGameRepository> create)
    {
        int passed = 0;
        async Task Test(string label, Func<Fixture, Clock, Task> action)
        {
            Clock clock = new(); Fixture f = await Fixture.Create(create(clock)); clock.Set(f.Now);
            await action(f, clock);
            Console.WriteLine($"PASS {provider} disconnection expiration: {label}"); passed++;
        }

        await Test("only recorded disconnects expire at the exact deadline", async (f, clock) =>
        {
            await Disconnect(f);
            string oldTicket = Guid.NewGuid().ToString("N"); await f.Ticket(oldTicket, f.A);
            clock.Set(f.Now.AddMinutes(10).AddTicks(-10));
            Require((await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default)).Count == 0,
                "A member was expelled before the grace deadline.");
            clock.Set(f.Now.AddMinutes(10));
            IReadOnlyList<DungeonDisconnectionExpulsion> receipts =
                await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default);
            Require(receipts.Count == 1 && receipts[0] is
                { CharacterId: var character, SteamId: var steam, DisconnectedAt: var disconnected,
                  ExpiredAt: var expired, ProcessedAt: var processed }
                && character == f.A.CharacterId && steam == f.A.SteamId && disconnected == f.Now
                && expired == f.Now.AddMinutes(10) && processed == expired,
                "Expiry receipt did not identify the original server-observed deadline.");
            DungeonSession session = await f.Read();
            DungeonMembershipSnapshot snapshot = (await f.Repo.GetDungeonMembershipSnapshotAsync(
                f.Session, f.Server, default))!;
            Require(session.State == DungeonSessionState.InProgress
                && session.Members.Select(member => member.CharacterId).SequenceEqual([f.B.CharacterId])
                && snapshot.Abandonments.Count == 0 && snapshot.DisconnectionExpulsions.SequenceEqual(receipts)
                && !await f.Repo.IsActiveDungeonSessionMemberAsync(f.Session, f.A.SteamId, f.A.CharacterId, f.Now, default)
                && await f.Repo.IsActiveDungeonSessionMemberAsync(f.Session, f.B.SteamId, f.B.CharacterId, f.Now, default),
                "Expiry affected the remaining party or masqueraded as explicit abandonment.");
            Require(await f.Consume(oldTicket) is null, "An old ticket admitted an expelled member.");
            Require((await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server,
                f.Now.AddMinutes(20), default)).SequenceEqual(receipts), "Retry changed an immutable expiry receipt.");
            DungeonSession next = await f.Repo.CreateDungeonSessionAsync(f.A.SteamId, f.A.CharacterId,
                "Dungeon.Test", "Hard", f.Now.AddMinutes(20), f.Expires, default);
            await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now.AddMinutes(21), default);
            Require((await f.Repo.GetActiveDungeonSessionForCharacterAsync(f.A.CharacterId,
                f.A.SteamId, f.Now.AddMinutes(21), default))?.DungeonSessionId == next.DungeonSessionId,
                "A retry against the old session erased the character's new lease.");
        });

        await Test("successful new admission cancels expiry, ticket consumption alone does not", async (f, clock) =>
        {
            await Disconnect(f);
            ConsumedJoinTicket reserved = await f.Admit();
            clock.Set(f.Now.AddMinutes(10));
            Require((await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default)).Count == 1,
                "An unconfirmed ticket cancelled disconnect expiry.");

            Clock recoveredClock = new(); Fixture recovered = await Fixture.Create(create(recoveredClock));
            recoveredClock.Set(recovered.Now);
            await Disconnect(recovered); ConsumedJoinTicket next = await recovered.Admit();
            recoveredClock.Set(recovered.Now.AddMinutes(9));
            Require(await recovered.Repo.ConfirmDungeonMemberAdmissionAsync(recovered.Session, recovered.Server,
                recovered.A.SteamId, recovered.A.CharacterId, next.ConnectionGeneration,
                recovered.Now.AddMinutes(9), default), "In-grace recovery was rejected.");
            recoveredClock.Set(recovered.Now.AddMinutes(20));
            Require((await recovered.Repo.ExpelDisconnectedDungeonMembersAsync(
                recovered.Session, recovered.Server, recovered.Now, default)).Count == 0
                && (await recovered.Read()).Members.Count == 2,
                "Confirmed recovery was later expelled by the original deadline.");
        });

        await Test("last expiry closes only its session and preserves a read-only receipt", async (f, clock) =>
        {
            await Disconnect(f); await Disconnect(f, f.B);
            string token = Guid.NewGuid().ToString("N");
            await f.Repo.StoreGameServerCredentialAsync(token, f.Server, f.Session, f.Expires, default);
            clock.Set(f.Now.AddMinutes(10));
            IReadOnlyList<DungeonDisconnectionExpulsion> receipts =
                await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default);
            DungeonSession closed = await f.Read();
            GameServerCredential? credential = await f.Repo.ResolveGameServerCredentialAsync(
                token, f.Now.AddMinutes(10), TimeSpan.FromMinutes(5), default);
            Require(receipts.Count == 2 && closed.Members.Count == 0 && closed.State == DungeonSessionState.Closed
                && closed.ExpiresAt == f.Now.AddMinutes(10)
                && credential is { IsSecurityTelemetryOnly: true }
                && (await f.Repo.GetDungeonMembershipSnapshotAsync(f.Session, f.Server, default))!
                    .DisconnectionExpulsions.SequenceEqual(receipts),
                "Last expiry lost its receipts or left an active empty party.");
        });

        await Test("settlement first preserves the captured reward recipients", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
            DungeonRewardSettlement settlement = await f.Settle();
            clock.Set(f.Now.AddMinutes(20));
            Require((await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default)).Count == 0
                && (await f.Read()).Members.Count == 2 && settlement.CharacterIds.Contains(f.A.CharacterId),
                "Post-settlement expiry retroactively changed captured rewards or membership.");
        });

        await Test("wrong server and natural session expiry never create expulsion evidence", async (f, clock) =>
        {
            await Disconnect(f);
            clock.Set(f.Now.AddMinutes(10));
            bool rejected = false;
            try { await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, "foreign", f.Now, default); }
            catch (DungeonSessionServerMismatchException) { rejected = true; }
            Require(rejected, "Foreign server expired a member.");
            clock.Set(f.Expires);
            Require((await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default)).Count == 0,
                "A naturally expired session created a client-disconnect receipt.");
        });
        await Test("server-only HTTP expiry and closed-session receipt read", CheckHttp);
        Console.WriteLine($"PASS {provider} disconnection expiration: {passed} scenarios.");
    }

    private static async Task CheckHttp(Fixture f, Clock clock)
    {
        await Disconnect(f);
        ConsumedJoinTicket secondAdmission = await f.Admit(f.B);
        await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.B.SteamId,
            f.B.CharacterId, secondAdmission.ConnectionGeneration, f.Now.AddMinutes(1), default);
        AccessTokenService tokens = new(clock);
        var player = tokens.Issue(TimeSpan.FromHours(1));
        var server = tokens.Issue(TimeSpan.FromHours(1));
        var foreign = tokens.Issue(TimeSpan.FromHours(1));
        Fixture otherSession = await Fixture.Create(f.Repo);
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(foreign.TokenHash, otherSession.Server,
            otherSession.Session, foreign.ExpiresAt, default);
        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo);
        builder.Services.AddSingleton<TimeProvider>(clock);
        builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>();
        app.MapDungeonAdmissionApi(); app.MapDungeonMembershipSnapshotApi();
        await app.StartAsync();
        try
        {
            using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
            string path = $"/api/dungeon-sessions/{f.Session:D}/disconnection-expirations";
            string snapshotPath = $"/api/dungeon-sessions/{f.Session:D}/membership-snapshot";
            async Task<HttpResponseMessage> Send(HttpMethod method, string target, string? token)
            {
                using HttpRequestMessage request = new(method, target);
                if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
                return await client.SendAsync(request);
            }
            clock.Set(f.Now.AddMinutes(10));
            using (var anonymous = await Send(HttpMethod.Post, path, null))
                Require(anonymous.StatusCode == HttpStatusCode.Unauthorized, "Anonymous expiry was accepted.");
            using (var user = await Send(HttpMethod.Post, path, player.Token))
                Require(user.StatusCode == HttpStatusCode.Forbidden, "Player token expired a party.");
            using (var wrong = await Send(HttpMethod.Post, path, foreign.Token))
                Require(wrong.StatusCode == HttpStatusCode.Forbidden, "Another session's server expired a party.");
            Require((await f.Read()).Members.Count == 2, "Rejected expiry request changed party membership.");
            using (var applied = await Send(HttpMethod.Post, path, server.Token))
            {
                Require(applied.StatusCode == HttpStatusCode.OK && applied.Headers.CacheControl?.NoStore == true,
                    "Assigned-server expiry did not return an uncached receipt.");
                string body = await applied.Content.ReadAsStringAsync();
                Require(body.Contains(f.A.CharacterId.ToString("D"), StringComparison.OrdinalIgnoreCase)
                    && !body.Contains(f.B.CharacterId.ToString("D"), StringComparison.OrdinalIgnoreCase),
                    "Expiry response included a still-in-grace member.");
            }
            using (var repeat = await Send(HttpMethod.Post, path, server.Token))
            {
                string body = await repeat.Content.ReadAsStringAsync();
                Require(repeat.StatusCode == HttpStatusCode.OK
                    && body.Contains(f.A.CharacterId.ToString("D"), StringComparison.OrdinalIgnoreCase)
                    && !body.Contains(f.B.CharacterId.ToString("D"), StringComparison.OrdinalIgnoreCase),
                    "Active-session retry lost or changed the immutable expiry receipt.");
            }
            clock.Set(f.Now.AddMinutes(11));
            using (var last = await Send(HttpMethod.Post, path, server.Token))
            {
                string body = await last.Content.ReadAsStringAsync();
                Require(last.StatusCode == HttpStatusCode.OK
                    && body.Contains(f.A.CharacterId.ToString("D"), StringComparison.OrdinalIgnoreCase)
                    && body.Contains(f.B.CharacterId.ToString("D"), StringComparison.OrdinalIgnoreCase),
                    "Last expiry response omitted a removed member.");
            }
            using (var repeat = await Send(HttpMethod.Post, path, server.Token))
                Require(repeat.StatusCode == HttpStatusCode.Forbidden,
                    "Closed-session credential retained mutation authority after last expulsion.");
            using (var read = await Send(HttpMethod.Get, snapshotPath, server.Token))
            {
                DungeonMembershipSnapshotResponse? snapshot =
                    await read.Content.ReadFromJsonAsync<DungeonMembershipSnapshotResponse>();
                Require(read.StatusCode == HttpStatusCode.OK && read.Headers.CacheControl?.NoStore == true
                    && snapshot is { State: "Closed" } && snapshot.Members.Count == 0
                    && snapshot.Abandonments.Count == 0 && snapshot.DisconnectionExpulsions.Count == 2,
                    "Closed-session read grace did not preserve distinct expulsion evidence.");
            }
        }
        finally { await app.StopAsync(); }
    }

    public static async Task CheckMemoryLock()
    {
        Clock clock = new(); InMemoryGameRepository repo = new(clock);
        Fixture f = await Fixture.Create(repo); await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
        object gate = typeof(InMemoryGameRepository).GetField("_dungeonGate", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(repo)!;
        Task<IReadOnlyList<DungeonDisconnectionExpulsion>> pending; Thread? waiting = null;
        lock (gate)
        {
            pending = Task.Run(async () =>
            {
                waiting = Thread.CurrentThread;
                return await repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default);
            });
            Require(SpinWait.SpinUntil(() => waiting is not null && (waiting.ThreadState & System.Threading.ThreadState.WaitSleepJoin) != 0,
                TimeSpan.FromSeconds(5)), "Expiry did not wait on the memory dungeon gate.");
            clock.Set(f.Now.AddMinutes(10));
        }
        Require((await pending.WaitAsync(TimeSpan.FromSeconds(5))).Count == 1,
            "Expiry used the pre-lock request time.");
        Console.WriteLine("PASS InMemory disconnection expiration: deadline crossed during dungeon-gate wait.");
    }

    public static async Task CheckPostgresLock(PrivateSchema schema)
    {
        Clock clock = new(); Fixture f = await Fixture.Create(schema.Repository(clock));
        await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
        await using NpgsqlConnection connection = await schema.Source.OpenConnectionAsync();
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync();
        await using (NpgsqlCommand hold = new(
            "SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id=@id FOR UPDATE;", connection, transaction))
        {
            hold.Parameters.AddWithValue("id", f.Session); await hold.ExecuteScalarAsync();
        }
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
        Task<IReadOnlyList<DungeonDisconnectionExpulsion>> pending = f.Repo.ExpelDisconnectedDungeonMembersAsync(
            f.Session, f.Server, f.Now.AddMinutes(9), timeout.Token);
        bool released = false;
        try
        {
            string application = new NpgsqlConnectionStringBuilder(schema.Source.ConnectionString).ApplicationName!;
            bool waiting = false; DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                await using var probe = schema.Source.CreateCommand(
                    "SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE application_name=@application AND wait_event_type='Lock' AND query LIKE '%FOR UPDATE%');");
                probe.Parameters.AddWithValue("application", application);
                if (await probe.ExecuteScalarAsync() is true) { waiting = true; break; }
                await Task.Delay(10);
            }
            Require(waiting && !pending.IsCompleted, "Expiry did not wait on the PostgreSQL parent row.");
            clock.Set(f.Now.AddMinutes(10)); await transaction.CommitAsync(); released = true;
            Require((await pending).Count == 1 && (await schema.Repository(clock).GetDungeonMembershipSnapshotAsync(
                f.Session, f.Server, default))!.DisconnectionExpulsions.Count == 1,
                "Expiry used the pre-lock time or lost its receipt across connections.");
            Console.WriteLine("PASS Postgres disconnection expiration: deadline crossed during parent-row wait; receipt persisted.");
        }
        finally
        {
            if (!released) { timeout.Cancel(); await transaction.RollbackAsync(); try { await pending; } catch (OperationCanceledException) { } }
        }
    }

    public static async Task CheckPostgresReceiptRollback(PrivateSchema schema)
    {
        Clock clock = new(); Fixture f = await Fixture.Create(schema.Repository(clock));
        await Disconnect(f);
        string ticket = Guid.NewGuid().ToString("N"); await f.Ticket(ticket, f.A);
        clock.Set(f.Now.AddMinutes(10));
        // The generated private schema alone receives this temporary fixture constraint.
        await schema.Execute($"ALTER TABLE dungeon_session_disconnection_expulsions ADD CONSTRAINT reject_fixture_expulsion CHECK (character_id <> '{f.A.CharacterId:D}'::uuid);");
        try
        {
            bool rejected = false;
            try { await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default); }
            catch (PostgresException) { rejected = true; }
            Require(rejected, "A rejected expulsion receipt unexpectedly committed.");
        }
        finally { await schema.Execute("ALTER TABLE dungeon_session_disconnection_expulsions DROP CONSTRAINT reject_fixture_expulsion;"); }
        DungeonSession afterFailure = await f.Read();
        Require(afterFailure.State == DungeonSessionState.InProgress && afterFailure.Members.Count == 2
            && afterFailure.Members.Single(member => member.CharacterId == f.A.CharacterId).DisconnectedAt == f.Now
            && (await f.Repo.GetDungeonMembershipSnapshotAsync(f.Session, f.Server, default))!
                .DisconnectionExpulsions.Count == 0,
            "Receipt failure partly committed membership removal or state.");
        // The old ticket is still present. Move only the test clock backwards to
        // distinguish retention from the intentional deadline rejection.
        clock.Set(f.Now.AddMinutes(9));
        Require((await f.Consume(ticket))?.CharacterId == f.A.CharacterId,
            "Receipt failure deleted the old ticket.");
        clock.Set(f.Now.AddMinutes(10));
        Require((await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default)).Count == 1,
            "A retry did not recover after receipt failure rollback.");
        Console.WriteLine("PASS Postgres disconnection expiration: receipt failure rolls back membership and tickets; retry recovers.");
    }
}
