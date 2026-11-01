using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
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

static class JoinTicketConsumptionSealTests
{
    private static string Hash() => (Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N")).ToUpperInvariant();
    private static Task<ConsumedJoinTicket?> Consume(Fixture f, string hash, Guid id)
        => f.Repo.ConsumeJoinTicketAsync(hash, f.Server, f.Session, f.Now, default, id);
    private static Task<JoinTicketConsumptionResolution?> Seal(Fixture f, string hash, Guid id)
        => f.Repo.SealJoinTicketConsumptionAsync(hash, f.Server, f.Session, id, f.A.SteamId, f.Now, default);

    public static async Task RunSuite(string provider, Func<TimeProvider, IGameRepository> create)
    {
        async Task Test(string name, Func<Fixture, FixedClock, Task> action)
        {
            FixedClock clock = new(DateTimeOffset.UtcNow); Fixture f = await Fixture.Create(create(clock)); clock.Set(f.Now);
            await action(f, clock); Console.WriteLine($"PASS {provider} consumption seal: {name}");
        }
        await Test("cancel existing ticket and fence ID/hash without touching members or rewards", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A); var before = await f.Read();
            var result = await Seal(f, hash, id);
            Require(result is { State: "Cancelled", Consumption: null, Cancellation: not null }
                && result.Cancellation.ConsumptionRequestId == id && result.Cancellation.TicketHash == hash
                && result.Cancellation.ExpectedSteamId == f.A.SteamId && result.Cancellation.CancelledAt == f.Now,
                "Cancellation proof does not identify the exact intent/peer/time.");
            Require(await Consume(f, hash, id) is null && await Consume(f, hash, Guid.NewGuid()) is null
                && await f.Consume(hash) is null, "A late consumption bypassed cancellation.");
            string other = Hash(); await f.Ticket(other, f.A);
            Require(await Consume(f, other, id) is null, "Sealed ID consumed a different ticket.");
            Require((await f.Read()).Members.SequenceEqual(before.Members), "Cancellation changed membership or connection state.");
            Require((await Consume(f, other, Guid.NewGuid()))?.ConnectionGeneration == 1, "Cancellation burned another ticket or generation.");
            Require((await f.Settle()).CharacterIds.Order().SequenceEqual(new[] { f.A.CharacterId, f.B.CharacterId }.Order()),
                "Cancellation removed an unconsumed member's reward eligibility.");
        });
        await Test("immutable retry survives ten minutes and a reinserted hash stays fenced", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var first = await Seal(f, hash, id); clock.Set(f.Now.AddMinutes(11));
            Require(await Seal(f, hash, id) == first, "Retry changed cancellation time or identity.");
            // Artificial hash reuse exercises the fence independently of deletion.
            await f.Ticket(hash, f.A);
            Require(await Consume(f, hash, Guid.NewGuid()) is null && await f.Consume(hash) is null,
                "Another ID or legacy consume bypassed the hash fence.");
            Require(await Seal(f, hash, Guid.NewGuid()) is null, "A different intent acquired the existing cancellation.");
        });
        await Test("still-present expired ticket is proof but missing/legacy history is not", async (f, clock) =>
        {
            string expired = Hash(); Guid id = Guid.NewGuid();
            await f.Repo.StoreJoinTicketAsync(expired, f.A.SteamId, f.A.CharacterId, f.Session, f.Now.AddSeconds(30), default);
            clock.Set(f.Now.AddMinutes(11));
            Require((await Seal(f, expired, id))?.State == "Cancelled", "Existing expired ticket could not be sealed.");
            Require(await Seal(f, Hash(), Guid.NewGuid()) is null, "Missing ticket was treated as no past commit.");
            string legacy = Hash(); await f.Ticket(legacy, f.A); await f.Consume(legacy);
            Require(await Seal(f, legacy, Guid.NewGuid()) is null, "Legacy consumption invented a non-consumption proof.");
            string cleaned = Hash(); await f.Repo.StoreJoinTicketAsync(cleaned, f.A.SteamId, f.A.CharacterId,
                f.Session, f.Now.AddMinutes(12), default);
            clock.Set(f.Now.AddMinutes(13)); await f.Ticket(Hash(), f.B); // Runs normal expired-ticket cleanup.
            Require(await Seal(f, cleaned, Guid.NewGuid()) is null, "Cleanup absence was treated as proof.");
        });
        await Test("wrong peer hash ID or assignment never burns a ticket or poisons an intent", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            Require(await f.Repo.SealJoinTicketConsumptionAsync(hash, f.Server, f.Session, id, f.B.SteamId, f.Now, default) is null,
                "Wrong original peer cancelled a stolen ticket.");
            Require(await Seal(f, Hash(), id) is null && await Seal(f, hash, Guid.Empty) is null,
                "Missing hash or empty ID accepted.");
            Require(await f.Repo.SealJoinTicketConsumptionAsync(hash, "foreign", f.Session, id, f.A.SteamId, f.Now, default) is null
                && await f.Repo.SealJoinTicketConsumptionAsync(hash, f.Server, Guid.NewGuid(), id, f.A.SteamId, f.Now, default) is null,
                "Foreign scope cancelled ticket.");
            Require(await Seal(f, hash, id) is { State: "Cancelled" }, "Denied calls poisoned the valid intent.");
            string other = Hash(); await f.Ticket(other, f.A);
            Require(await Seal(f, other, id) is null
                && await f.Repo.SealJoinTicketConsumptionAsync(hash, f.Server, f.Session, id, f.B.SteamId, f.Now, default) is null,
                "Cancellation collision exposed or overwrote a proof.");
            Require(await Consume(f, other, Guid.NewGuid()) is not null, "Collision consumed an unrelated ticket.");
        });
        await Test("consume winner returns historical evidence even after replay expires or generation changes", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var consumed = await Consume(f, hash, id) ?? throw new TestFailure("Initial consume failed.");
            clock.Set(f.Now.AddMinutes(11)); await f.Admit(); var before = await f.Read();
            var result = await Seal(f, hash, id);
            Require(result is { State: "Recorded", Cancellation: null, Consumption: not null }
                && result.Consumption.ConnectionGeneration == consumed.ConnectionGeneration
                && result.Consumption.ConsumedAt == f.Now, "Seal cancelled a committed/superseded result.");
            Require((await f.Read()).Members.SequenceEqual(before.Members) && await Consume(f, hash, id) is null,
                "Seal revived or rewound admission authority.");
            string other = Hash(); await f.Ticket(other, f.A);
            Require(await Seal(f, other, id) is null && await Seal(f, hash, Guid.NewGuid()) is null,
                "Receipt collision incorrectly cancelled a different ticket/intent.");
            Require(await Consume(f, other, Guid.NewGuid()) is not null, "Receipt collision burned a ticket.");
        });
        await Test("committed loss and abandonment remain unchanged and remaining party can settle", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var consumed = await Consume(f, hash, id) ?? throw new TestFailure("Initial consume failed.");
            await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId, f.A.CharacterId,
                consumed.ConnectionGeneration, f.Now, default);
            clock.Set(f.Now.AddMinutes(9)); var before = await f.Read();
            Require((await Seal(f, hash, id))?.State == "Recorded" && (await f.Read()).Members.SequenceEqual(before.Members),
                "Resolution reset disconnect grace.");
            await f.Abandon();
            Require((await Seal(f, hash, id))?.State == "Recorded" && (await f.Read()).Members.Single().CharacterId == f.B.CharacterId,
                "Historical resolution revived abandoned membership.");
            Require((await f.Settle()).CharacterIds.SequenceEqual([f.B.CharacterId]), "Resolution changed remaining-party rewards.");
            Require(await Seal(f, hash, id) is null, "Ended session authorized sealing.");
        });
        await Test("expired assignment rejects new seals and retries", async (f, clock) =>
        {
            string hash = Hash(), other = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A); await f.Ticket(other, f.B);
            await Seal(f, hash, id); clock.Set(f.Expires);
            Require(await Seal(f, hash, id) is null && await Seal(f, other, Guid.NewGuid()) is null,
                "Expired server assignment authorized cancellation.");
        });
        await Test("actual HTTP authentication validation nested results and unknown fail-closed", CheckHttp);
    }

    private static async Task CheckHttp(Fixture f, FixedClock clock)
    {
        AccessTokenService tokens = new(clock); var server = tokens.Issue(TimeSpan.FromHours(1)); var player = tokens.Issue(TimeSpan.FromHours(1));
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        string rawTicket = Hash(), hash = tokens.Hash(rawTicket); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
        WebApplicationBuilder builder = WebApplication.CreateBuilder(); builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo); builder.Services.AddSingleton<TimeProvider>(clock); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using WebApplication app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapJoinTicketApi(); await app.StartAsync();
        try
        {
            using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()) };
            var body = new SealJoinTicketConsumptionRequest(f.Session, f.Server, id, hash.ToLowerInvariant(), f.A.SteamId);
            async Task<HttpResponseMessage> Send(string? token, SealJoinTicketConsumptionRequest value)
            {
                using HttpRequestMessage request = new(HttpMethod.Post, "/api/join-tickets/seal-consumption");
                if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
                request.Content = JsonContent.Create(value); return await client.SendAsync(request);
            }
            using (var response = await Send(null, body)) Require(response.StatusCode == HttpStatusCode.Unauthorized, "Anonymous seal allowed.");
            using (var response = await Send(player.Token, body)) Require(response.StatusCode == HttpStatusCode.Forbidden, "Player seal allowed.");
            foreach (var scope in new[] { body with { DungeonSessionId = Guid.NewGuid() }, body with { ServerId = "foreign" } })
                using (var response = await Send(server.Token, scope)) Require(response.StatusCode == HttpStatusCode.Forbidden, "Foreign assignment seal allowed.");
            foreach (var invalid in new[] { body with { ConsumptionRequestId = Guid.Empty }, body with { DungeonSessionId = Guid.Empty },
                body with { ServerId = "bad/server" }, body with { TicketHash = new string('x', 64) }, body with { TicketHash = null! },
                body with { ExpectedSteamId = "１２" }, body with { ExpectedSteamId = "" }, body with { ExpectedSteamId = null! } })
                using (var response = await Send(server.Token, invalid)) Require(response.StatusCode == HttpStatusCode.BadRequest, "Malformed seal accepted.");
            foreach (var unknown in new[] { body with { TicketHash = new string('0', 64) }, body with { ExpectedSteamId = f.B.SteamId } })
                using (var response = await Send(server.Token, unknown))
                    Require(response.StatusCode == HttpStatusCode.Conflict && response.Headers.CacheControl?.NoStore == true
                        && (await response.Content.ReadFromJsonAsync<Dictionary<string, string>>())?.GetValueOrDefault("error") == "join_ticket_consumption_unresolved",
                        "Unknown seal cleared uncertainty.");
            JoinTicketConsumptionResolution? first;
            using (var response = await Send(server.Token, body))
            {
                string json = await response.Content.ReadAsStringAsync(); using JsonDocument envelope = JsonDocument.Parse(json);
                first = JsonSerializer.Deserialize<JoinTicketConsumptionResolution>(json, new JsonSerializerOptions(JsonSerializerDefaults.Web));
                Require(response.StatusCode == HttpStatusCode.OK && response.Headers.CacheControl?.NoStore == true
                    && first is { State: "Cancelled", Consumption: null, Cancellation: not null }
                    && first.Cancellation.TicketHash == hash && first.Cancellation.ConsumptionRequestId == id
                    && !envelope.RootElement.TryGetProperty("characterId", out _), "Cancellation envelope could be mistaken for admission.");
            }
            using (var response = await Send(server.Token, body))
                Require(await response.Content.ReadFromJsonAsync<JoinTicketConsumptionResolution>() == first, "Lost-response retry changed proof.");
            Require(await Consume(f, hash, id) is null, "HTTP cancellation did not block delayed consume.");
            foreach (Guid? lateId in new Guid?[] { id, Guid.NewGuid(), null })
            {
                using HttpRequestMessage late = new(HttpMethod.Post, "/api/join-tickets/consume");
                late.Headers.Authorization = new AuthenticationHeaderValue("Bearer", server.Token);
                late.Content = JsonContent.Create(new ConsumeJoinTicketRequest(rawTicket, f.Server, lateId));
                using var response = await client.SendAsync(late);
                Require(response.StatusCode == HttpStatusCode.Unauthorized
                    && (await response.Content.ReadFromJsonAsync<Dictionary<string, string>>())?.GetValueOrDefault("error") == "join_ticket_not_recoverable",
                    "Late HTTP consume bypassed the cancellation fence.");
            }
            string recordedHash = tokens.Hash(Hash()); Guid recordedId = Guid.NewGuid(); await f.Ticket(recordedHash, f.A); await Consume(f, recordedHash, recordedId);
            using (var response = await Send(server.Token, body with { TicketHash = recordedHash, ConsumptionRequestId = recordedId }))
                Require((await response.Content.ReadFromJsonAsync<JoinTicketConsumptionResolution>()) is { State: "Recorded", Consumption: not null, Cancellation: null },
                    "HTTP consume-winner response was not historical evidence.");
            await f.Settle();
            using (var response = await Send(server.Token, body)) Require(response.StatusCode == HttpStatusCode.Forbidden, "Telemetry-only credential gained seal permission.");
        }
        finally { await app.StopAsync(); }
    }

    public static async Task CheckPostgresTransactions(PrivateSchema schema)
    {
        FixedClock precisionClock = new(DateTimeOffset.UtcNow);
        var precision = await Fixture.Create(schema.Repository(precisionClock));
        precisionClock.Set(precision.Now.AddTicks(7)); // Deliberately below PostgreSQL's microsecond precision.
        string precisionHash = Hash(); Guid precisionId = Guid.NewGuid(); await precision.Ticket(precisionHash, precision.A);
        var initialProof = await Seal(precision, precisionHash, precisionId);
        var repeatedProof = await schema.Repository(precisionClock).SealJoinTicketConsumptionAsync(precisionHash,
            precision.Server, precision.Session, precisionId, precision.A.SteamId, precision.Now, default);
        Require(initialProof is { State: "Cancelled", Cancellation: not null } && initialProof == repeatedProof
            && initialProof.Cancellation.CancelledAt.Ticks % 10 == 0,
            "First cancellation proof differs from its persisted PostgreSQL timestamp.");
        Console.WriteLine("PASS Postgres consumption seal: first response uses the persisted microsecond timestamp");

        FixedClock clock = new(DateTimeOffset.UtcNow); var f = await Fixture.Create(schema.Repository(clock)); clock.Set(f.Now);
        string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
        await schema.Execute($"ALTER TABLE game_join_ticket_consumption_cancellations ADD CONSTRAINT reject_fixture_seal CHECK (consumption_request_id <> '{id:D}'::uuid);");
        try
        {
            bool rejected = false; try { await Seal(f, hash, id); } catch (PostgresException) { rejected = true; }
            Require(rejected, "Fixture constraint failed to reject cancellation INSERT.");
        }
        finally { await schema.Execute("ALTER TABLE game_join_ticket_consumption_cancellations DROP CONSTRAINT reject_fixture_seal;"); }
        var first = await Seal(f, hash, id);
        Require(first is { State: "Cancelled" }, "INSERT failure did not roll ticket deletion back.");
        clock.Set(f.Now.AddMinutes(11)); var fresh = schema.Repository(clock);
        Require(await fresh.SealJoinTicketConsumptionAsync(hash, f.Server, f.Session, id, f.A.SteamId, f.Now, default) == first
            && await fresh.ConsumeJoinTicketAsync(hash, f.Server, f.Session, f.Now, default, id) is null,
            "Cancellation/fence did not survive a fresh repository connection.");
        Console.WriteLine("PASS Postgres consumption seal: INSERT rollback and fresh-repository proof/fence");

        await CheckOrderedRace(schema, sealFirst: true, expire: false);
        await CheckOrderedRace(schema, sealFirst: false, expire: false);
        await CheckOrderedRace(schema, sealFirst: true, expire: true);

        var purged = await Fixture.Create(schema.Repository(clock)); clock.Set(purged.Now);
        string purgedHash = Hash(); Guid purgedId = Guid.NewGuid(); await purged.Ticket(purgedHash, purged.A); await Consume(purged, purgedHash, purgedId);
        // Emulate an older deployment's removed evidence, in this private schema only.
        await using (var command = schema.Source.CreateCommand("DELETE FROM game_join_ticket_consumptions WHERE consumption_request_id=@id;"))
        { command.Parameters.AddWithValue("id", purgedId); await command.ExecuteNonQueryAsync(); }
        var before = await purged.Read();
        Require(await Seal(purged, purgedHash, purgedId) is null && (await purged.Read()).Members.SequenceEqual(before.Members),
            "Purged evidence became a fabricated cancellation.");
        Console.WriteLine("PASS Postgres consumption seal: purged historical commit remains unresolved");
    }

    private static async Task CheckOrderedRace(PrivateSchema schema, bool sealFirst, bool expire)
    {
        FixedClock clock = new(DateTimeOffset.UtcNow); var f = await Fixture.Create(schema.Repository(clock)); clock.Set(f.Now);
        string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
        await using var connection = await schema.Source.OpenConnectionAsync(); await using var transaction = await connection.BeginTransactionAsync();
        await using (var command = new NpgsqlCommand("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id=@id FOR UPDATE;", connection, transaction))
        { command.Parameters.AddWithValue("id", f.Session); await command.ExecuteScalarAsync(); }
        async Task WaitBlocked(int expected)
        {
            string name = new NpgsqlConnectionStringBuilder(schema.Source.ConnectionString).ApplicationName ?? throw new TestFailure("Missing fixture application name.");
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                await using var command = schema.Source.CreateCommand("SELECT COUNT(*) FROM pg_stat_activity WHERE application_name=@name AND wait_event_type='Lock' AND query LIKE '%FOR UPDATE%';");
                command.Parameters.AddWithValue("name", name);
                if ((long)(await command.ExecuteScalarAsync())! >= expected) return;
                await Task.Delay(20);
            }
            throw new TestFailure("Operations did not serialize on the parent session lock.");
        }
        Task<JoinTicketConsumptionResolution?>? seal = null; Task<ConsumedJoinTicket?>? consume = null; bool committed = false;
        try
        {
            if (sealFirst) seal = Seal(f, hash, id); else consume = Consume(f, hash, id);
            await WaitBlocked(1);
            if (sealFirst) consume = Consume(f, hash, id); else seal = Seal(f, hash, id);
            await WaitBlocked(2);
            Require(!seal!.IsCompleted && !consume!.IsCompleted, "A mutation bypassed the parent lock.");
            if (expire) clock.Set(f.Expires);
            await transaction.CommitAsync(); committed = true;
            var resolution = await seal; var admission = await consume!;
            if (expire) Require(resolution is null && admission is null, "Pre-lock clock revived expired server authority.");
            else if (sealFirst) Require(resolution is { State: "Cancelled" } && admission is null, "Delayed consume bypassed the winning seal.");
            else Require(resolution is { State: "Recorded", Consumption: not null, Cancellation: null }
                && admission is not null && resolution.Consumption.ConnectionGeneration == admission.ConnectionGeneration,
                "Seal cancelled an earlier committed consumption.");
            Console.WriteLine($"PASS Postgres consumption seal: queued {(expire ? "expiry after lock wait" : sealFirst ? "seal before consume" : "consume before seal")}");
        }
        finally
        {
            if (!committed) await transaction.RollbackAsync();
            if (seal is not null) await seal; if (consume is not null) await consume;
        }
    }
}
