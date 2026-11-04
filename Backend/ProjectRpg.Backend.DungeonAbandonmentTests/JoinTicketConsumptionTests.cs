using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
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
using static Tests;

static class JoinTicketConsumptionTests
{
    private static string Hash() => Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
    private static Task<ConsumedJoinTicket?> Consume(Fixture f, string hash, Guid? id)
        => f.Repo.ConsumeJoinTicketAsync(hash, f.Server, f.Session, f.Now, default, id);

    public static async Task RunSuite(string provider, Func<TimeProvider, IGameRepository> create)
    {
        async Task Test(string name, Func<Fixture, FixedClock, Task> action)
        {
            FixedClock clock = new(DateTimeOffset.UtcNow);
            Fixture f = await Fixture.Create(create(clock)); clock.Set(f.Now);
            await action(f, clock);
            Console.WriteLine($"PASS {provider} consumption recovery: {name}");
        }
        await Test("same intent recovers after ticket TTL without minting another generation", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid();
            await f.Repo.StoreJoinTicketAsync(hash, f.A.SteamId, f.A.CharacterId, f.Session, f.Now.AddSeconds(1), default);
            ConsumedJoinTicket first = await Consume(f, hash, id) ?? throw new TestFailure("Initial consume rejected.");
            DungeonSession before = await f.Read(); clock.Set(f.Now.AddMinutes(2));
            Require(await Consume(f, hash, id) == first, "Lost response could not be recovered after ticket expiry.");
            Require((await f.Read()).Members.SequenceEqual(before.Members), "Receipt recovery mutated member state.");
            Require(await Consume(f, hash, null) is null, "Receipt allowed legacy ticket replay.");
        });
        await Test("legacy one-use and nonempty request ID remain mandatory", async (f, _) =>
        {
            string hash = Hash(); await f.Ticket(hash, f.A);
            Require(await Consume(f, hash, Guid.Empty) is null, "Empty intent was accepted.");
            Require(await Consume(f, hash, null) is not null, "Empty intent burned the ticket.");
            Require(await Consume(f, hash, null) is null, "Legacy replay succeeded.");
            Require(await Consume(f, hash, Guid.NewGuid()) is null, "Legacy consume gained a retroactive receipt.");
        });
        await Test("receipt is bound to request, ticket, server and session", async (f, _) =>
        {
            string hash = Hash(), other = Hash(); Guid id = Guid.NewGuid();
            await f.Ticket(hash, f.A); await f.Ticket(other, f.B);
            var first = await Consume(f, hash, id);
            Require(first is not null, "Initial consume failed.");
            Require(await Consume(f, hash, Guid.NewGuid()) is null, "Different intent recovered a consumed ticket.");
            Require(await Consume(f, other, id) is null, "One intent consumed two tickets.");
            Require(await f.Repo.ConsumeJoinTicketAsync(hash, "another-server", f.Session, f.Now, default, id) is null,
                "Another server recovered the receipt.");
            Require(await f.Repo.ConsumeJoinTicketAsync(hash, f.Server, Guid.NewGuid(), f.Now, default, id) is null,
                "Another session recovered the receipt.");
            Require(await Consume(f, hash, id) == first, "Denied scope destroyed the original receipt.");
            Require(await Consume(f, other, Guid.NewGuid()) is not null, "Denied reuse burned the other member's ticket.");
        });
        await Test("concurrent retries reserve exactly one generation", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var results = await Task.WhenAll(Enumerable.Range(0, 16).Select(_ => Task.Run(() => Consume(f, hash, id))));
            Require(results.All(result => result is not null && result == results[0]), "Concurrent retries returned different results.");
            Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).ConnectionGeneration == 1,
                "Concurrent retries minted another generation.");
        });
        await Test("newer admission supersedes the receipt without being changed", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var old = await Consume(f, hash, id); var current = await f.Admit();
            Require(old is not null && current.ConnectionGeneration > old.ConnectionGeneration, "Fixture did not advance.");
            Require(await Consume(f, hash, id) is null, "An obsolete generation was recovered for admission.");
            Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).ConnectionGeneration == current.ConnectionGeneration,
                "Old receipt rewound the current generation.");
        });
        await Test("recovery is not admission confirmation and does not restart disconnect grace", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            var admission = await Consume(f, hash, id) ?? throw new TestFailure("No admission.");
            await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, admission.ConnectionGeneration, f.Now, default);
            clock.Set(f.Now.AddMinutes(9)); Require(await Consume(f, hash, id) == admission, "In-grace recovery failed.");
            var member = (await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId);
            Require(member.DisconnectedAt == f.Now && member.ConfirmedConnectionGeneration == 0,
                "Receipt confirmed admission or reset the first disconnect.");
            clock.Set(f.Now.AddMinutes(10)); Require(await Consume(f, hash, id) is null, "Exact grace deadline was bypassed.");
            Require((await f.Settle()).CharacterIds.SequenceEqual([f.B.CharacterId]), "Recovery restored expired reward eligibility.");
        });
        await Test("exact receipt retention expires even while membership is live", async (f, clock) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            Require(await Consume(f, hash, id) is not null, "Initial consume failed.");
            clock.Set(f.Now + JoinTicketConsumptionRecovery.Retention - TimeSpan.FromTicks(10));
            Require(await Consume(f, hash, id) is not null, "Receipt expired too early.");
            clock.Set(f.Now + JoinTicketConsumptionRecovery.Retention);
            Require(await Consume(f, hash, id) is null, "Receipt never expired.");
            Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).ConnectionGeneration == 1,
                "Expired receipt created a fresh generation.");
        });
        await Test("abandonment and settlement cannot be undone by recovery", async (f, _) =>
        {
            string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
            await Consume(f, hash, id); await f.Abandon();
            Require(await Consume(f, hash, id) is null, "Abandoned member recovered admission.");
            Require((await f.Read()).Members.Single().CharacterId == f.B.CharacterId, "Remaining party changed.");
            Fixture settled = await Fixture.Create(f.Repo); string second = Hash(); Guid secondId = Guid.NewGuid();
            await settled.Ticket(second, settled.A); await Consume(settled, second, secondId); await settled.Settle();
            Require(await Consume(settled, second, secondId) is null, "Settlement was undone by receipt replay.");
        });
        await Test("production HTTP enforces server scope and retry identity", CheckHttp);
    }

    private static async Task CheckHttp(Fixture f, FixedClock clock)
    {
        AccessTokenService tokens = new(clock);
        var server = tokens.Issue(TimeSpan.FromHours(1)); var player = tokens.Issue(TimeSpan.FromHours(1));
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        string rawTicket = Hash(); string hash = tokens.Hash(rawTicket); Guid id = Guid.NewGuid();
        await f.Ticket(hash, f.A);
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
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapJoinTicketApi(); await app.StartAsync();
        try
        {
            using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()) };
            async Task<HttpResponseMessage> Send(string? token, Guid? intent, string? serverId = null)
            {
                using HttpRequestMessage request = new(HttpMethod.Post, "/api/join-tickets/consume");
                if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
                request.Content = JsonContent.Create(new ConsumeJoinTicketRequest(rawTicket, serverId ?? f.Server, intent));
                return await client.SendAsync(request);
            }
            using (var anonymous = await Send(null, id)) Require(anonymous.StatusCode == HttpStatusCode.Unauthorized, "Anonymous recovery allowed.");
            using (var foreign = await Send(player.Token, id)) Require(foreign.StatusCode == HttpStatusCode.Forbidden, "Player consumed a server ticket.");
            using (var wrongServer = await Send(server.Token, id, "different-server")) Require(wrongServer.StatusCode == HttpStatusCode.Forbidden, "Wrong server accepted.");
            using (var empty = await Send(server.Token, Guid.Empty)) Require(empty.StatusCode == HttpStatusCode.BadRequest, "Empty request ID accepted.");
            ConsumeJoinTicketResponse? first;
            using (var initial = await Send(server.Token, id))
            {
                Require(initial.StatusCode == HttpStatusCode.OK && initial.Headers.CacheControl?.NoStore == true, "Initial consume/cache policy failed.");
                first = await initial.Content.ReadFromJsonAsync<ConsumeJoinTicketResponse>();
            }
            // Deliberately discard the first transport response at the caller's
            // logical boundary, then retry the very same production POST.
            using (var retry = await Send(server.Token, id))
                Require(retry.StatusCode == HttpStatusCode.OK && await retry.Content.ReadFromJsonAsync<ConsumeJoinTicketResponse>() == first,
                    "HTTP recovery minted a different result.");
            using (var changed = await Send(server.Token, Guid.NewGuid()))
                Require(changed.StatusCode == HttpStatusCode.Unauthorized
                    && (await changed.Content.ReadFromJsonAsync<Dictionary<string, string>>())?.GetValueOrDefault("error") == "join_ticket_not_recoverable",
                    "Changed ID recovered a ticket or authenticated denial lacked its terminal marker.");
            using (var legacy = await Send(server.Token, null)) Require(legacy.StatusCode == HttpStatusCode.Unauthorized, "Legacy replay became reusable.");
            await f.Abandon();
            using (var revoked = await Send(server.Token, id)) Require(revoked.StatusCode == HttpStatusCode.Unauthorized, "HTTP recovery revived abandonment.");
        }
        finally { await app.StopAsync(); }
    }

    public static async Task CheckPostgresAtomicity(PrivateSchema schema)
    {
        FixedClock clock = new(DateTimeOffset.UtcNow); Fixture f = await Fixture.Create(schema.Repository(clock)); clock.Set(f.Now);
        string hash = Hash(); Guid id = Guid.NewGuid(); await f.Ticket(hash, f.A);
        // Fixture-generated UUID, private schema only. Force the final INSERT
        // to fail, so a partial ticket deletion/generation increment is observable.
        await schema.Execute($"ALTER TABLE game_join_ticket_consumptions ADD CONSTRAINT reject_fixture_consumption CHECK (character_id <> '{f.A.CharacterId:D}'::uuid);");
        try
        {
            bool rejected = false;
            try { await Consume(f, hash, id); }
            catch (Npgsql.PostgresException) { rejected = true; }
            Require(rejected, "Fixture receipt constraint did not reject the transaction.");
        }
        finally { await schema.Execute("ALTER TABLE game_join_ticket_consumptions DROP CONSTRAINT reject_fixture_consumption;"); }
        Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).ConnectionGeneration == 0,
            "Receipt failure committed a generation increment.");
        var first = await Consume(f, hash, id) ?? throw new TestFailure("Rollback lost the ticket.");
        var recovered = await schema.Repository(clock).ConsumeJoinTicketAsync(hash, f.Server, f.Session, f.Now, default, id);
        Require(recovered == first && first.ConnectionGeneration == 1, "Receipt did not survive a new repository/connection.");
        Console.WriteLine("PASS Postgres consumption recovery: atomic rollback and fresh-repository persistence");
    }
}
