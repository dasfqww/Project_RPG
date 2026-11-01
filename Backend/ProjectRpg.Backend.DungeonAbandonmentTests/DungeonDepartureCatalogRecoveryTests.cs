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
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class DungeonDepartureCatalogRecoveryTests
{
    private const string Admin = "fixture-departure-admin";

    public static async Task Run()
    {
        InMemoryGameRepository repository = new();
        Fixture f = await Fixture.Create(repository, DungeonSessionState.Waiting);
        TimeProvider clock = new FixedClock(f.Now);
        AccessTokenService tokens = new(clock);
        IssuedAccessToken leader = tokens.Issue(TimeSpan.FromHours(1));
        IssuedAccessToken member = tokens.Issue(TimeSpan.FromHours(1));
        IssuedAccessToken expired = tokens.Issue(TimeSpan.FromSeconds(-1));
        foreach (var (token, owner) in new[] { (leader, f.A.SteamId), (member, f.B.SteamId), (expired, f.A.SteamId) })
            await repository.StoreSessionAsync(token.TokenHash, owner, token.ExpiresAt, default);
        DungeonSession initial = await f.Read();
        DungeonDepartureRequest command = new(f.A.CharacterId, Guid.NewGuid(), initial.LobbyRevision);

        // Same repository across two real HTTP hosts models a changed process
        // catalog without mutating the deliberately immutable catalog snapshot.
        await using (Host supported = await Host.Start(repository, clock, tokens, "supported"))
        {
            using HttpResponseMessage response = await supported.Send(f, leader.Token, command);
            Require(response.StatusCode == HttpStatusCode.OK, $"Supported departure did not commit: {response.StatusCode}.");
        }
        DungeonSession queued = await f.Read();
        Require(queued.DepartureRequestedAt.HasValue, "No committed departure fixture.");
        Console.WriteLine("PASS departure catalog: supported HTTP command commits before host/catalog replacement");

        await using Host removed = await Host.Start(repository, clock, tokens, "removed");
        await AssertReplay(removed, f, leader.Token, command, "Waiting");
        Console.WriteLine("PASS departure catalog: committed Waiting replay survives removed content without renewal");

        await using (Host difficultyRemoved = await Host.Start(repository, clock, tokens, "difficulty-removed"))
        {
            await AssertReplay(difficultyRemoved, f, leader.Token, command, "Waiting");
            string before = Snapshot(await f.Read());
            using var differentId = await difficultyRemoved.Send(f, leader.Token, command with { RequestId = Guid.NewGuid() });
            Require(differentId.StatusCode == HttpStatusCode.Conflict, "Unsupported difficulty allowed a new request.");
            Require(Snapshot(await f.Read()) == before, "Difficulty rejection changed session state.");
        }
        Console.WriteLine("PASS departure catalog: removed difficulty permits only the exact committed request");

        string waiting = Snapshot(await f.Read());
        foreach (DungeonDepartureRequest wrong in new[] {
            command with { RequestId = Guid.NewGuid() },
            command with { ExpectedLobbyRevision = queued.LobbyRevision } })
        {
            using var rejected = await removed.Send(f, leader.Token, wrong);
            Require(rejected.StatusCode == HttpStatusCode.Conflict, "Unknown command bypassed the catalog.");
        }
        foreach (var (token, request, status) in new[] {
            ((string?)null, command, HttpStatusCode.Unauthorized),
            ("invalid-token", command, HttpStatusCode.Unauthorized),
            (expired.Token, command, HttpStatusCode.Unauthorized),
            (member.Token, command, HttpStatusCode.Forbidden),
            (Admin, command, HttpStatusCode.Forbidden),
            (leader.Token, command with { CharacterId = f.B.CharacterId }, HttpStatusCode.Forbidden),
            (leader.Token, command with { RequestId = Guid.Empty }, HttpStatusCode.BadRequest),
            (leader.Token, command with { ExpectedLobbyRevision = 0 }, HttpStatusCode.BadRequest) })
        {
            using var rejected = await removed.Send(f, token, request);
            Require(rejected.StatusCode == status, "Replay weakened principal/request guards.");
        }
        Require(Snapshot(await f.Read()) == waiting, "Rejected request changed revision, party or lease.");
        Console.WriteLine("PASS departure catalog: request pair, bearer, leader, character and shape guards unchanged");

        await repository.AssignDungeonServerAsync(f.Session, f.Server, "127.0.0.1:7777", f.Now, f.Expires,
            default, requireDeparture: true, minimumPartySize: 2);
        await AssertReplay(removed, f, leader.Token, command, "Loading");
        await repository.StartDungeonSessionAsync(f.Session, f.Server, f.Now, f.Expires, default);
        await AssertReplay(removed, f, leader.Token, command, "InProgress");
        Console.WriteLine("PASS departure catalog: allocated/started replays retain exact assignment and full state");

        string inProgress = Snapshot(await f.Read());
        using (var cancel = await removed.Send(f, leader.Token, command, cancel: true))
            Require(cancel.StatusCode == HttpStatusCode.Conflict, "Cancellation undid a claimed departure.");
        Require(Snapshot(await f.Read()) == inProgress, "Denied cancellation changed the running party.");
        await repository.FinishDungeonSessionAsync(f.Session, f.Server, DungeonSessionState.Failed, f.Now, default);
        string terminal = Snapshot(await f.Read());
        using (var replay = await removed.Send(f, leader.Token, command))
            Require(replay.StatusCode == HttpStatusCode.Conflict, "Terminal session was requeued.");
        Require(Snapshot(await f.Read()) == terminal, "Terminal replay mutated state.");
        Console.WriteLine("PASS departure catalog: claimed cancellation and terminal replay cannot reopen a session");

        Fixture cancelled = await Fixture.Create(repository, DungeonSessionState.Waiting);
        IssuedAccessToken cancelLeader = tokens.Issue(TimeSpan.FromHours(1));
        await repository.StoreSessionAsync(cancelLeader.TokenHash, cancelled.A.SteamId, cancelLeader.ExpiresAt, default);
        DungeonSession basis = await cancelled.Read();
        DungeonDepartureRequest cancelCommand = new(cancelled.A.CharacterId, Guid.NewGuid(), basis.LobbyRevision);
        await repository.SetDungeonDepartureAsync(cancelled.Session, cancelled.A.SteamId, cancelled.A.CharacterId,
            cancelCommand.RequestId, cancelCommand.ExpectedLobbyRevision, false, 2, f.Now, default);
        using (var cancel = await removed.Send(cancelled, cancelLeader.Token, cancelCommand, cancel: true))
            Require(cancel.StatusCode == HttpStatusCode.OK, "Removed content prevented safe cancellation.");
        string afterCancel = Snapshot(await cancelled.Read());
        using (var replay = await removed.Send(cancelled, cancelLeader.Token, cancelCommand))
            Require(replay.StatusCode == HttpStatusCode.Conflict, "Cancelled command was resurrected.");
        Require(Snapshot(await cancelled.Read()) == afterCancel, "Cancelled request replay changed state.");
        Console.WriteLine("PASS departure catalog: removed content still cancels safely; cancelled receipt cannot requeue");
    }

    private static async Task AssertReplay(Host host, Fixture f, string token, DungeonDepartureRequest command, string state)
    {
        DungeonSession before = await f.Read();
        string snapshot = Snapshot(before);
        using HttpResponseMessage response = await host.Send(f, token, command);
        Require(response.StatusCode == HttpStatusCode.OK,
            $"Committed replay rejected after catalog removal: {response.StatusCode} ({state}).");
        DungeonSessionResponse? body = await response.Content.ReadFromJsonAsync<DungeonSessionResponse>();
        Require(response.Headers.CacheControl?.NoStore == true && body is not null
            && body.DungeonSessionId == f.Session && body.State == state
            && body.DepartureRequestId == command.RequestId && body.DepartureRequestRevision == command.ExpectedLobbyRevision
            && body.LobbyRevision == before.LobbyRevision && body.ExpiresAt == before.ExpiresAt
            && body.DepartureRequestedAt == before.DepartureRequestedAt, "Replay response changed committed identity/revision/lease.");
        Require(Snapshot(await f.Read()) == snapshot, "Replay mutated full session state.");
    }

    private static string Snapshot(DungeonSession session) => JsonSerializer.Serialize(session);
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }

    private sealed class Host(WebApplication app, HttpClient client) : IAsyncDisposable
    {
        public static async Task<Host> Start(IGameRepository repository, TimeProvider clock, AccessTokenService tokens, string mode)
        {
            WebApplicationBuilder builder = WebApplication.CreateBuilder();
            builder.Configuration.Sources.Clear();
            builder.Configuration.AddInMemoryCollection();
            builder.Logging.ClearProviders();
            builder.WebHost.UseUrls("http://127.0.0.1:0");
            builder.Services.AddSingleton(repository);
            builder.Services.AddSingleton(clock);
            builder.Services.AddSingleton(tokens);
            builder.Services.AddSingleton(new DungeonContentCatalog(new DungeonContentOptions { Entries = [new()
            {
                DungeonId = mode == "removed" ? "Dungeon.Other" : "Dungeon.Test",
                Difficulties = [mode == "difficulty-removed" ? "Normal" : "Hard"], MapPackage = "/Game/Maps/testmap"
            }] }));
            builder.Services.AddSingleton<IOptions<DungeonSessionOptions>>(Options.Create(new DungeonSessionOptions { MinimumDeparturePartySize = mode == "supported" ? 2 : 3 }));
            builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = Admin }));
            builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
            builder.Services.AddScoped<BearerAuthenticator>();
            WebApplication app = builder.Build();
            try
            {
                app.UseMiddleware<ApiAuthenticationMiddleware>();
                app.MapDungeonDepartureApi();
                await app.StartAsync();
                return new(app, new HttpClient { BaseAddress = new Uri(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) });
            }
            catch { await app.DisposeAsync(); throw; }
        }

        public async Task<HttpResponseMessage> Send(Fixture f, string? token, DungeonDepartureRequest command, bool cancel = false)
        {
            using HttpRequestMessage request = new(HttpMethod.Post,
                $"/api/dungeon-sessions/{f.Session:D}/departure" + (cancel ? "/cancel" : ""));
            request.Content = JsonContent.Create(command);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            return await client.SendAsync(request);
        }

        public async ValueTask DisposeAsync()
        {
            client.Dispose();
            await app.StopAsync();
            await app.DisposeAsync();
        }
    }
}
