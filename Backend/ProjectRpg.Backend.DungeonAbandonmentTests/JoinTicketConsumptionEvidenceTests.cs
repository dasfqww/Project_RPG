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

static class JoinTicketConsumptionEvidenceTests
{
    private static string Hash() => Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
    private static Task<ConsumedJoinTicket?> Consume(Fixture f, string hash, Guid id)
        => f.Repo.ConsumeJoinTicketAsync(hash, f.Server, f.Session, f.Now, default, id);
    private static Task<JoinTicketConsumptionEvidence?> Read(Fixture f, string hash, Guid id)
        => f.Repo.ReadJoinTicketConsumptionEvidenceAsync(hash, f.Server, f.Session, id, f.A.SteamId, f.Now, default);

    public static async Task RunSuite(string provider, Func<TimeProvider, IGameRepository> create)
    {
        async Task Test(string name, Func<Fixture, FixedClock, Task> action)
        {
            FixedClock clock = new(DateTimeOffset.UtcNow); Fixture f = await Fixture.Create(create(clock)); clock.Set(f.Now);
            await action(f, clock); Console.WriteLine($"PASS {provider} consumption evidence: {name}");
        }
        await Test("expired admission evidence survives later ticket consumption", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var admitted = await Consume(f, hash, id) ?? throw new TestFailure("Initial consume failed.");
            clock.Set(f.Now.AddMinutes(11));
            Require(await Consume(f, hash, id) is null, "Evidence extended admission replay.");
            await f.Admit(f.B); // Former cleanup on a later consume must not erase evidence.
            var before = await f.Read();
            var evidence = await Read(f, hash, id);
            Require(evidence is not null && evidence.ConnectionGeneration == admitted.ConnectionGeneration
                && evidence.CharacterId == f.A.CharacterId && evidence.ConsumedAt == f.Now
                && evidence.ConsumptionRequestId == id && evidence.ServerId == f.Server, "Historical receipt lost or altered.");
            Require((await f.Read()).Members.SequenceEqual(before.Members), "Reading evidence mutated membership.");
            string other = Hash(); await f.Ticket(other, f.A);
            Require(await Consume(f, other, id) is null, "Expired request ID was reused and overwrote evidence.");
            Require(await Consume(f, other, Guid.NewGuid()) is not null, "Denied reuse burned another ticket.");
        });
        await Test("exact request hash, observed peer identity and assignment required", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A); await Consume(f, hash, id);
            Require(await Read(f, Hash(), id) is null && await Read(f, hash, Guid.NewGuid()) is null,
                "Foreign request/hash exposed evidence.");
            Require(await f.Repo.ReadJoinTicketConsumptionEvidenceAsync(hash, f.Server, f.Session, id,
                f.B.SteamId, f.Now, default) is null, "A stolen ticket bypassed the original peer identity.");
            Require(await f.Repo.ReadJoinTicketConsumptionEvidenceAsync(hash, "foreign-server", f.Session, id,
                f.A.SteamId, f.Now, default) is null, "Foreign server obtained evidence.");
            Require(await f.Repo.ReadJoinTicketConsumptionEvidenceAsync(hash, f.Server, Guid.NewGuid(), id,
                f.A.SteamId, f.Now, default) is null, "Foreign session obtained evidence.");
            Require(await Read(f, hash, Guid.Empty) is null && await Read(f, hash, id) is not null, "Denied lookup destroyed valid evidence.");
        });
        await Test("missing and legacy records remain unresolved and lookup never consumes", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            Require(await Read(f, hash, id) is null, "Read invented a receipt.");
            Require((await Consume(f, hash, id))?.ConnectionGeneration == 1, "Read consumed or reserved a generation.");
            string legacy = Hash(); await f.Ticket(legacy, f.A); await f.Consume(legacy);
            Require(await Read(f, legacy, Guid.NewGuid()) is null, "Legacy consume acquired invented evidence.");
        });
        await Test("superseded receipt stays historical and never reactivates the old generation", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var old = await Consume(f, hash, id) ?? throw new TestFailure("No old generation.");
            var current = await f.Admit(); var before = await f.Read();
            Require((await Read(f, hash, id))?.ConnectionGeneration == old.ConnectionGeneration,
                "Historical read lost the original generation.");
            Require(await Consume(f, hash, id) is null, "Evidence granted an obsolete admission.");
            Require(current.ConnectionGeneration > old.ConnectionGeneration && (await f.Read()).Members.SequenceEqual(before.Members),
                "Reading the old result rewound active authority.");
        });
        await Test("abandonment remains immutable while remaining party may finish", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A); await Consume(f, hash, id); await f.Abandon();
            Require(await Read(f, hash, id) is not null, "Removed-member evidence disappeared.");
            Require((await f.Read()).Members.Single().CharacterId == f.B.CharacterId, "Read restored abandoned membership.");
            Require((await f.Settle()).CharacterIds.SequenceEqual([f.B.CharacterId]), "Read restored abandoned rewards.");
            Require(await Read(f, hash, id) is null, "Evidence read extended authority after settlement.");
        });
        await Test("late evidence cannot clear a disconnect or extend reward grace", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var admission = await Consume(f, hash, id) ?? throw new TestFailure("No admission.");
            await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, admission.ConnectionGeneration, f.Now, default);
            clock.Set(f.Now.AddMinutes(11)); var before = await f.Read();
            Require(await Read(f, hash, id) is not null, "Late disconnect evidence unavailable.");
            Require((await f.Read()).Members.SequenceEqual(before.Members), "Historical read changed a disconnect or confirmation.");
            Require((await f.Settle()).CharacterIds.SequenceEqual([f.B.CharacterId]), "Read extended disconnected reward eligibility.");
        });
        await Test("expired server assignment does not authorize historical lookup", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A); await Consume(f, hash, id);
            clock.Set(f.Expires);
            Require(await Read(f, hash, id) is null, "Expired assignment read evidence.");
        });
        await Test("real HTTP is scoped, non-cacheable, read-only and not an admission response", CheckHttp);
    }

    private static async Task CheckHttp(Fixture f, FixedClock clock)
    {
        AccessTokenService tokens = new(clock); var server = tokens.Issue(TimeSpan.FromHours(1)); var player = tokens.Issue(TimeSpan.FromHours(1));
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        string hash = tokens.Hash(Hash()); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
        var consumed = await Consume(f, hash, id) ?? throw new TestFailure("Initial consume failed.");
        clock.Set(f.Now.AddMinutes(11));
        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo); builder.Services.AddSingleton<TimeProvider>(clock); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        StoryAdmissionTestServices.AddUnrestricted(builder.Services);
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapJoinTicketApi(); await app.StartAsync();
        try
        {
            using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()) };
            var body = new ReadJoinTicketConsumptionRequest(f.Session, f.Server, id, hash.ToLowerInvariant(), f.A.SteamId);
            async Task<HttpResponseMessage> Send(string? token, ReadJoinTicketConsumptionRequest requestBody)
            {
                using HttpRequestMessage request = new(HttpMethod.Post, "/api/join-tickets/consumption-evidence");
                if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
                request.Content = JsonContent.Create(requestBody); return await client.SendAsync(request);
            }
            using (var anonymous = await Send(null, body)) Require(anonymous.StatusCode == HttpStatusCode.Unauthorized, "Anonymous lookup allowed.");
            using (var foreign = await Send(player.Token, body)) Require(foreign.StatusCode == HttpStatusCode.Forbidden, "Player read server evidence.");
            using (var foreignScope = await Send(server.Token, body with { DungeonSessionId = Guid.NewGuid() }))
                Require(foreignScope.StatusCode == HttpStatusCode.Forbidden, "Cross-session lookup accepted.");
            foreach (var invalid in new[] { body with { ConsumptionRequestId = Guid.Empty }, body with { TicketHash = new string('x', 64) },
                body with { ExpectedSteamId = "foreign\npeer" }, body with { TicketHash = "" } })
                using (var response = await Send(server.Token, invalid)) Require(response.StatusCode == HttpStatusCode.BadRequest, "Invalid lookup accepted.");
            foreach (var unknown in new[] { body with { ExpectedSteamId = f.B.SteamId }, body with { ConsumptionRequestId = Guid.NewGuid() },
                body with { TicketHash = new string('0', 64) } })
                using (var response = await Send(server.Token, unknown))
                    Require(response.StatusCode == HttpStatusCode.Conflict
                        && (await response.Content.ReadFromJsonAsync<Dictionary<string, string>>())?.GetValueOrDefault("error") == "join_ticket_consumption_unresolved",
                        "Missing/foreign evidence implied a transaction never committed.");
            var before = await f.Read();
            using (var response = await Send(server.Token, body))
            {
                Require(response.StatusCode == HttpStatusCode.OK && response.Headers.CacheControl?.NoStore == true, "Evidence cache/status contract failed.");
                string json = await response.Content.ReadAsStringAsync();
                var read = JsonSerializer.Deserialize<ReadJoinTicketConsumptionResponse>(json, new JsonSerializerOptions(JsonSerializerDefaults.Web));
                using JsonDocument envelope = JsonDocument.Parse(json);
                Require(read is { State: "Recorded" } && read.Consumption.ConnectionGeneration == consumed.ConnectionGeneration
                    && read.Consumption.ConsumptionRequestId == id && read.Consumption.SteamId == f.A.SteamId
                    && !envelope.RootElement.TryGetProperty("characterId", out _), "Historical response could masquerade as admission.");
            }
            Require((await f.Read()).Members.SequenceEqual(before.Members) && await Consume(f, hash, id) is null,
                "HTTP evidence read mutated members or extended replay eligibility.");
            await f.Settle();
            using (var ended = await Send(server.Token, body)) Require(ended.StatusCode == HttpStatusCode.Forbidden,
                "Telemetry-only credential gained a new post-session permission.");
        }
        finally { await app.StopAsync(); }
    }

    public static async Task CheckPostgresPersistenceAndLock(PrivateSchema schema)
    {
        FixedClock clock = new(DateTimeOffset.UtcNow); var f = await Fixture.Create(schema.Repository(clock)); clock.Set(f.Now);
        string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A); await Consume(f, hash, id);
        clock.Set(f.Now.AddMinutes(11));
        var evidence = await schema.Repository(clock).ReadJoinTicketConsumptionEvidenceAsync(hash, f.Server, f.Session, id, f.A.SteamId, f.Now, default);
        Require(evidence is not null && evidence.ConsumedAt == f.Now, "Expired evidence did not survive a fresh repository connection.");
        await using var connection = await schema.Source.OpenConnectionAsync();
        await using var transaction = await connection.BeginTransactionAsync();
        await using (var command = new NpgsqlCommand("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id=@id FOR UPDATE;", connection, transaction))
        { command.Parameters.AddWithValue("id", f.Session); await command.ExecuteScalarAsync(); }
        var pending = Read(f, hash, id);
        bool committed = false;
        try
        {
            string appName = new NpgsqlConnectionStringBuilder(schema.Source.ConnectionString).ApplicationName
                ?? throw new TestFailure("Private-schema application name is required for the lock check.");
            bool blocked = false; DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                await using var check = schema.Source.CreateCommand("SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE application_name=@name AND wait_event_type='Lock' AND query LIKE '%FOR UPDATE%');");
                check.Parameters.AddWithValue("name", appName);
                if (await check.ExecuteScalarAsync() is true) { blocked = true; break; }
                await Task.Delay(20);
            }
            Require(blocked && !pending.IsCompleted, "Evidence read did not serialize with admission/settlement.");
            clock.Set(f.Expires); await transaction.CommitAsync(); committed = true;
            Require(await pending is null, "Evidence read used a pre-lock clock and revived expired authority.");
        }
        finally { if (!committed) await transaction.RollbackAsync(); await pending; }
        Console.WriteLine("PASS Postgres consumption evidence: fresh-repository persistence and locked-time authorization");
    }
}
