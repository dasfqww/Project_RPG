using System.Diagnostics;
using System.Globalization;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
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
using static Tests;

// Opt-in loopback integration fixture, never mapped into the production app.
// Separate UE processes use production consume/evidence/seal/outbox transports.
static class UnrealConsumptionRecoveryTests
{
    private const string TestPath = "ProjectRPG.Integration.DungeonConsumeHttpRestart";
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { WriteIndented = true };
    private sealed class Clock : TimeProvider
    {
        public TimeSpan Offset { get; set; }
        public override DateTimeOffset GetUtcNow() => DateTimeOffset.UtcNow + Offset;
    }
    private sealed class Faults
    {
        public int ConsumeCalls, EvidenceCalls, SealCalls, DisconnectionCalls, DroppedConsume, DroppedDisconnection;
        public int RejectedConsume, DroppedSeal;
        public bool CancelUnconsumed { get; init; }
        public string? SealDropGate { get; init; }
        public List<string> DisconnectionBodies { get; } = [];
        public List<string> SealBodies { get; } = [];
        public List<string> SealResponses { get; } = [];
        public async Task Invoke(HttpContext context, RequestDelegate next)
        {
            string path = context.Request.Path.Value ?? "";
            bool consume = path == "/api/join-tickets/consume";
            bool disconnect = path.EndsWith("/disconnection", StringComparison.Ordinal);
            bool seal = path == "/api/join-tickets/seal-consumption";
            if (path == "/api/join-tickets/consumption-evidence") Interlocked.Increment(ref EvidenceCalls);
            if (seal) Interlocked.Increment(ref SealCalls);
            if (!consume && !disconnect && !(CancelUnconsumed && seal)) { await next(context); return; }
            if (consume)
            {
                int attempt = Interlocked.Increment(ref ConsumeCalls);
                if (CancelUnconsumed && attempt == 1)
                {
                    // Explicit pre-endpoint fault: no ticket is consumed. Missing
                    // evidence alone is still never treated as proof of that fact.
                    Interlocked.Increment(ref RejectedConsume);
                    context.Response.StatusCode = StatusCodes.Status503ServiceUnavailable;
                    await context.Response.WriteAsJsonAsync(new { error = "fixture_before_consumption" });
                    return;
                }
            }
            else
            {
                if (disconnect) Interlocked.Increment(ref DisconnectionCalls);
                context.Request.EnableBuffering();
                using StreamReader reader = new(context.Request.Body, Encoding.UTF8, leaveOpen: true);
                string body = await reader.ReadToEndAsync(context.RequestAborted);
                context.Request.Body.Position = 0;
                if (seal) { lock (SealBodies) SealBodies.Add(body); }
                else { lock (DisconnectionBodies) DisconnectionBodies.Add(body); } // Identity/generation/time only.
            }
            Stream original = context.Response.Body;
            await using MemoryStream buffered = new();
            context.Response.Body = buffered;
            try
            {
                await next(context); // The actual authenticated API/repository commits first.
                if (seal) { lock (SealResponses) SealResponses.Add(Encoding.UTF8.GetString(buffered.ToArray())); }
                bool drop = context.Response.StatusCode == 200 && (CancelUnconsumed
                    ? seal && Interlocked.CompareExchange(ref DroppedSeal, 1, 0) == 0
                    : consume ? Interlocked.CompareExchange(ref DroppedConsume, 1, 0) == 0
                        : Interlocked.CompareExchange(ref DroppedDisconnection, 1, 0) == 0);
                if (drop)
                {
                    if (seal && SealDropGate is not null)
                        await File.WriteAllTextAsync(SealDropGate, "seal-response-dropped");
                    context.Abort(); return;
                }
                buffered.Position = 0;
                await buffered.CopyToAsync(original, context.RequestAborted);
            }
            finally { context.Response.Body = original; }
        }
    }

    public static async Task Run(string[] args)
    {
        string Option(string name) => args.SkipWhile(x => x != name).Skip(1).FirstOrDefault()
            ?? throw new TestFailure($"Missing fixture option {name}.");
        string project = Path.GetFullPath(Option("--ue-project")), editor = Path.GetFullPath(Option("--ue-editor"));
        string root = Path.GetFullPath(Option("--run-directory"));
        string projectRoot = Path.GetDirectoryName(project)!;
        string allowed = Path.Combine(projectRoot, "Saved", "Tests", "DungeonConsumeHttpRestart") + Path.DirectorySeparatorChar;
        Require(File.Exists(project) && Path.GetFileName(project) == "Project_RPG.uproject"
            && File.Exists(editor) && Path.GetFileName(editor) == "UnrealEditor-Cmd.exe", "Invalid project/editor fixture inputs.");
        Require(root.StartsWith(allowed, StringComparison.OrdinalIgnoreCase)
            && Guid.TryParseExact(Path.GetFileName(root), "N", out _), "Run directory must be an owned Saved/Tests GUID.");
        int timeout = int.Parse(Option("--timeout-seconds"), CultureInfo.InvariantCulture);
        Require(timeout is >= 60 and <= 600, "Invalid process timeout.");
        Directory.CreateDirectory(root);
        string module = Path.Combine(projectRoot, "Binaries", "Win64", "UnrealEditor-Project_RPG.dll");
        string moduleHash = HashFile(module);
        string[] inputs = [
            "Source/Project_RPG/Private/Tests/RPGDungeonDisconnectionOrderingTests.cpp",
            "Source/Project_RPG/Private/Tests/RPGDungeonEjectionTestFixture.h",
            "Source/Project_RPG/Private/Tests/RPGDungeonEjectionTestFixture.cpp",
            "Source/Project_RPG/Public/Manager/HttpWebManager.h", "Source/Project_RPG/Private/Manager/HttpWebManager.cpp",
            "Source/Project_RPG/Public/Online/RPGDungeonConsumeJournal.h", "Source/Project_RPG/Private/Online/RPGDungeonConsumeJournal.cpp",
            "Source/Project_RPG/Public/Online/RPGDungeonDisconnectionJournal.h", "Source/Project_RPG/Private/Online/RPGDungeonDisconnectionJournal.cpp",
            "Source/Project_RPG/Public/Online/RPGJoinTicketAdmission.h", "Source/Project_RPG/Private/Online/RPGJoinTicketAdmission.cpp",
            "Source/Project_RPG/Project_RPG.Build.cs", "Backend/Database/schema.sql",
            "Backend/ProjectRpg.Backend.DungeonAbandonmentTests/Program.cs",
            "Backend/ProjectRpg.Backend.DungeonAbandonmentTests/UnrealConsumptionRecoveryTests.cs",
            "Backend/ProjectRpg.Backend.DungeonAbandonmentTests/JoinTicketConsumptionSealTests.cs",
            "Backend/ProjectRpg.Backend/Api/JoinTicketApiEndpoints.cs", "Backend/ProjectRpg.Backend/Api/DungeonAdmissionApiEndpoints.cs",
            "Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.cs", "Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.JoinTicketSeal.cs",
            "Backend/ProjectRpg.Backend/Data/PostgresGameRepository.cs", "Backend/ProjectRpg.Backend/Data/PostgresGameRepository.JoinTicketSeal.cs",
            "Backend/ProjectRpg.Backend/Contracts/ApiContracts.cs", "Backend/ProjectRpg.Backend/Domain/JoinTicketConsumptionRecovery.cs",
            "Backend/ProjectRpg.Backend/Domain/DungeonConnectionPolicy.cs", "Scripts/Network/Run-DungeonConsumeHttpRestartTest.ps1"
        ];
        var sourceHashes = inputs.ToDictionary(path => path, path => HashFile(Path.Combine(projectRoot, path)));
        bool postgres = args.Contains("--require-postgres");
        bool cancelUnconsumed = args.Contains("--cancel-unconsumed");
        PrivateSchema? schema = null;
        try
        {
            Clock clock = new();
            IGameRepository repo;
            if (postgres)
            {
                string connection = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES")
                    ?? throw new TestFailure("PostgreSQL mode requires the loopback test connection.");
                schema = await PrivateSchema.Create(connection); repo = schema.Repository(clock);
            }
            else repo = new InMemoryGameRepository(clock);
            Fixture f = await Fixture.Create(repo);
            AccessTokenService tokens = new(clock);
            var credential = tokens.Issue(TimeSpan.FromHours(1));
            await repo.StoreGameServerCredentialAsync(credential.TokenHash, f.Server, f.Session, credential.ExpiresAt, default);
            string rawTicket = tokens.Issue(TimeSpan.FromMinutes(5)).Token, ticketHash = tokens.Hash(rawTicket);
            await f.Ticket(ticketHash, f.A);
            var partyBefore = await f.Read();
            Faults faults = new() { CancelUnconsumed = cancelUnconsumed, SealDropGate = Path.Combine(root, "seal-response-dropped.gate") };
            WebApplicationBuilder builder = WebApplication.CreateBuilder(); builder.Configuration.Sources.Clear();
            builder.Configuration.AddInMemoryCollection(); builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
            builder.Services.AddSingleton(repo); builder.Services.AddSingleton<TimeProvider>(clock); builder.Services.AddSingleton(tokens);
            builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
            builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
            builder.Services.AddScoped<BearerAuthenticator>();
            await using WebApplication app = builder.Build();
            app.Use(faults.Invoke); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapJoinTicketApi(); app.MapDungeonAdmissionApi();
            await app.StartAsync();
            try
            {
                string backend = app.Urls.Single(), journal = Path.Combine(root, "journal");
                var environment = new Dictionary<string, string> {
                    ["UE_SKIP_UBT_SDK_SETUP"] = "1",
                    ["PROJECT_RPG_RECOVERY_TEST_BACKEND"] = backend + "/api",
                    ["PROJECT_RPG_GAME_SERVER_ID"] = f.Server, ["PROJECT_RPG_DUNGEON_SESSION_ID"] = f.Session.ToString("D"),
                    ["PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN"] = credential.Token,
                    ["PROJECT_RPG_RECOVERY_TEST_DIRECTORY"] = journal, ["PROJECT_RPG_RECOVERY_TEST_STEAM"] = f.A.SteamId
                };
                if (cancelUnconsumed) environment["PROJECT_RPG_RECOVERY_TEST_CANCEL"] = "1";
                var seed = await RunUnreal("Seed", rawTicket, environment, editor, project, root, timeout);
                Require(faults.ConsumeCalls == 1 && faults.DisconnectionCalls == 0
                    && (cancelUnconsumed ? faults.RejectedConsume == 1 && faults.DroppedConsume == 0 : faults.DroppedConsume == 1),
                    "Seed did not preserve the configured uncertain consume/no-disconnection boundary.");
                string[] losses = Directory.GetFiles(Path.Combine(journal, "ConsumeRecoveries"), "*.json");
                Require(losses.Length == 1, "Seed did not preserve exactly one observed-consumption journal.");
                string contents = await File.ReadAllTextAsync(losses[0]);
                using JsonDocument observation = JsonDocument.Parse(contents);
                JsonElement saved = observation.RootElement;
                Guid id = saved.GetProperty("requestId").GetGuid();
                DateTimeOffset observed = saved.GetProperty("observedAtUtc").GetDateTimeOffset();
                Require(saved.GetProperty("ticketHash").GetString() == ticketHash && saved.GetProperty("expectedSteamId").GetString() == f.A.SteamId
                    && saved.GetProperty("serverId").GetString() == f.Server && saved.GetProperty("sessionId").GetGuid() == f.Session
                    && saved.GetProperty("backendUrl").GetString() == backend + "/api"
                    && !contents.Contains(rawTicket, StringComparison.Ordinal) && !contents.Contains(credential.Token, StringComparison.Ordinal),
                    "Durable observation changed original identity/scope or persisted a secret.");
                await File.WriteAllTextAsync(Path.Combine(root, "seed-journal.snapshot.json"), contents);
                if (cancelUnconsumed)
                {
                    await RunCancellation(f, repo, clock, tokens, credential.Token, rawTicket, ticketHash, id, observed, losses[0], contents, partyBefore,
                        faults, environment, seed, editor, project, root, timeout, postgres, module, moduleHash, projectRoot, sourceHashes);
                    return;
                }
                var committed = await repo.ReadJoinTicketConsumptionEvidenceAsync(ticketHash, f.Server, f.Session, id, f.A.SteamId, clock.GetUtcNow(), default);
                Require(committed is not null && committed.CharacterId == f.A.CharacterId && committed.ConnectionGeneration == 1,
                    "The dropped response did not correspond to a real committed generation.");
                Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).DisconnectedAt is null,
                    "Seeding a local observation fabricated a backend disconnect before recovery.");
                var resumed = await RunUnreal("Resume", null, environment, editor, project, root, timeout);
                Require(seed.ProcessId != resumed.ProcessId && faults.ConsumeCalls == 1 && faults.EvidenceCalls >= 1 && faults.SealCalls == 0
                    && faults.DroppedDisconnection == 1 && faults.DisconnectionCalls >= 2,
                    "Restart did not use historical evidence and retry the lost real disconnection ACK without reconsuming.");
                string[] bodies; lock (faults.DisconnectionBodies) bodies = faults.DisconnectionBodies.ToArray();
                Require(bodies.All(body => body == bodies[0]), "Disconnection retry changed the original body/time/generation.");
                using JsonDocument report = JsonDocument.Parse(bodies[0]);
                Require(report.RootElement.GetProperty("steamId").GetString() == f.A.SteamId
                    && report.RootElement.GetProperty("connectionGeneration").GetInt64() == committed!.ConnectionGeneration
                    && report.RootElement.GetProperty("observedAtUtc").GetDateTimeOffset() == observed,
                    "Recovered disconnection body changed the first actual fixture observation.");
                var after = await f.Read(); var member = after.Members.Single(m => m.CharacterId == f.A.CharacterId);
                Require(member.DisconnectedAt == observed && member.DisconnectedGeneration == committed!.ConnectionGeneration
                    && member.ConnectionGeneration == committed.ConnectionGeneration && after.Members.Count == 2
                    && after.Members.Single(m => m.CharacterId == f.B.CharacterId) == partyBefore.Members.Single(m => m.CharacterId == f.B.CharacterId),
                    "Recovery rewrote grace time/generation or changed the remaining party.");
                Require(Directory.GetFiles(journal, "*.json*", SearchOption.AllDirectories).Length == 0,
                    "UE reported success before durable journal cleanup.");
                using HttpClient client = new() { BaseAddress = new Uri(backend) };
                client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", credential.Token);
                clock.Offset = TimeSpan.FromMinutes(11); // Explicit accelerated clock, not natural wall-clock grace.
                using var expiration = await client.PostAsync($"/api/dungeon-sessions/{f.Session:D}/disconnection-expirations", null);
                Require(expiration.IsSuccessStatusCode, "Actual expiry API rejected recovered observation.");
                var expiredParty = await f.Read();
                Require(expiredParty.Members.Count == 1 && expiredParty.Members[0].CharacterId == f.B.CharacterId,
                    "Elapsed grace did not remove only the recovered disconnected member.");
                var settlement = await f.Settle();
                Require(settlement.CharacterIds.SequenceEqual(new[] { f.B.CharacterId }), "Expired recovered member retained unsettled reward eligibility.");
                Require(moduleHash == HashFile(module) && sourceHashes.All(x => HashFile(Path.Combine(projectRoot, x.Key)) == x.Value),
                    "Selected inputs/module changed during HTTP restart integration.");
                await File.WriteAllTextAsync(Path.Combine(root, "proof.json"), JsonSerializer.Serialize(new {
                    schemaVersion = 1, provider = postgres ? "Postgres" : "InMemory", editorModuleSha256 = moduleHash, sourceHashes,
                    backendFixtureSha256 = HashFile(typeof(UnrealConsumptionRecoveryTests).Assembly.Location),
                    seed, resumed, consumptionRequestId = id, connectionGeneration = committed!.ConnectionGeneration, observedAtUtc = observed,
                    faults.ConsumeCalls, faults.EvidenceCalls, faults.DisconnectionCalls, faults.DroppedConsume, faults.DroppedDisconnection,
                    scope = new { actualHttp = true, actualPostgres = postgres, separateUnrealProcesses = true,
                        productionConsumeEvidenceAndOutbox = true, disconnectionAckLoss = true, noReadmission = true,
                        simulatedOriginalPeerLoss = true, acceleratedGraceExpiry = true, remainingPartyPreserved = true,
                        expiredMemberExcludedFromSettlement = true, actualIrisClients = false, osCrash = false, actualSteam = false }
                }, Json));
                Console.WriteLine($"PASS { (postgres ? "Postgres" : "InMemory") } UE consumption HTTP restart: {Path.Combine(root, "proof.json")}");
            }
            finally { await app.StopAsync(); }
        }
        finally { if (schema is not null) await schema.DisposeAsync(); }
    }

    private static async Task RunCancellation(Fixture f, IGameRepository repo, Clock clock, AccessTokenService tokens,
        string token, string rawTicket, string ticketHash, Guid id, DateTimeOffset observed, string lossPath, string originalJournal,
        DungeonSession partyBefore, Faults faults, Dictionary<string, string> environment, PhaseResult seed,
        string editor, string project, string root, int timeout, bool postgres, string module, string moduleHash,
        string projectRoot, Dictionary<string, string> sourceHashes)
    {
        Require(await repo.ReadJoinTicketConsumptionEvidenceAsync(ticketHash, f.Server, f.Session, id, f.A.SteamId, clock.GetUtcNow(), default) is null
            && (await f.Read()).Members.SequenceEqual(partyBefore.Members), "Pre-endpoint fault mutated consumption or the party.");
        var sealLost = await RunUnreal("SealLost", null, environment, editor, project, root, timeout);
        Require(faults.SealCalls == 1 && faults.DroppedSeal == 1 && faults.ConsumeCalls == 1 && faults.DisconnectionCalls == 0,
            "Seal-loss phase did not commit exactly one cancellation without reconsumption/disconnection.");
        Require(File.Exists(lossPath) && await File.ReadAllTextAsync(lossPath) == originalJournal,
            "Lost cancellation response prematurely rewrote or deleted the original observed intent.");
        string firstResponse; lock (faults.SealResponses) firstResponse = faults.SealResponses.Single();
        var cancellation = JsonSerializer.Deserialize<JoinTicketConsumptionResolution>(firstResponse, Json)?.Cancellation
            ?? throw new TestFailure("The dropped response was not an actual cancellation proof.");
        Require(cancellation.DungeonSessionId == f.Session && cancellation.ServerId == f.Server && cancellation.ConsumptionRequestId == id
            && cancellation.TicketHash == ticketHash && cancellation.ExpectedSteamId == f.A.SteamId && cancellation.CancelledAt >= observed,
            "Dropped cancellation proof changed the original assignment/nonce/hash/peer.");
        Require((await f.Read()).Members.SequenceEqual(partyBefore.Members), "Cancellation created a connection generation, disconnect or membership change.");
        var resumed = await RunUnreal("Resume", null, environment, editor, project, root, timeout);
        Require(new[] { seed.ProcessId, sealLost.ProcessId, resumed.ProcessId }.Distinct().Count() == 3
            && faults.ConsumeCalls == 1 && faults.EvidenceCalls == 2 && faults.SealCalls == 2 && faults.DroppedSeal == 1
            && faults.DisconnectionCalls == 0 && faults.DroppedDisconnection == 0,
            "Cancellation restart did not re-read/reseal the original intent without admission or invented loss.");
        string[] requests, responses;
        lock (faults.SealBodies) requests = faults.SealBodies.ToArray();
        lock (faults.SealResponses) responses = faults.SealResponses.ToArray();
        Require(requests.Length == 2 && requests[0] == requests[1], "Restart changed the exact original sealing request body.");
        var replay = JsonSerializer.Deserialize<JoinTicketConsumptionResolution>(responses[1], Json);
        Require(replay is { State: "Cancelled", Consumption: null } && replay.Cancellation == cancellation,
            "Idempotent cancellation changed its original proof/time across UE restart.");
        Require(Directory.GetFiles(Path.GetDirectoryName(lossPath)!, "*.json*", SearchOption.AllDirectories).Length == 0
            && Directory.GetFiles(environment["PROJECT_RPG_RECOVERY_TEST_DIRECTORY"], "*.json*", SearchOption.TopDirectoryOnly).Length == 0
            && (await f.Read()).Members.SequenceEqual(partyBefore.Members),
            "Verified cleanup did not resolve only its own journal, or changed party/generation state.");
        using HttpClient client = new() { BaseAddress = new Uri(environment["PROJECT_RPG_RECOVERY_TEST_BACKEND"] + "/") };
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
        async Task Denied(string ticket, Guid? requestId)
        {
            using var response = await client.PostAsJsonAsync("join-tickets/consume", new ConsumeJoinTicketRequest(ticket, f.Server, requestId));
            Require(response.StatusCode == HttpStatusCode.Unauthorized
                && (await response.Content.ReadFromJsonAsync<Dictionary<string, string>>())?.GetValueOrDefault("error") == "join_ticket_not_recoverable",
                "A late consume bypassed the exact cancellation/hash/nonce fence.");
        }
        await Denied(rawTicket, id); await Denied(rawTicket, Guid.NewGuid()); await Denied(rawTicket, null);
        string newTicket = tokens.Issue(TimeSpan.FromMinutes(5)).Token;
        await f.Ticket(tokens.Hash(newTicket), f.A);
        await Denied(newTicket, id); // A sealed UUID cannot migrate to a different ticket.
        Require((await f.Read()).Members.SequenceEqual(partyBefore.Members), "Rejected late attempts burned a generation or changed the remaining party.");
        Guid freshId = Guid.NewGuid();
        using (var admitted = await client.PostAsJsonAsync("join-tickets/consume", new ConsumeJoinTicketRequest(newTicket, f.Server, freshId)))
        {
            var value = await admitted.Content.ReadFromJsonAsync<ConsumeJoinTicketResponse>();
            Require(admitted.IsSuccessStatusCode && value is not null && value.CharacterId == f.A.CharacterId
                && value.SteamId == f.A.SteamId && value.ConnectionGeneration == 1,
                "Cancellation poisoned an unrelated fresh ticket/nonce or burned a generation.");
        }
        var after = await f.Read();
        Require(after.Members.Count == 2 && after.Members.Single(m => m.CharacterId == f.A.CharacterId).DisconnectedAt is null
            && after.Members.Single(m => m.CharacterId == f.B.CharacterId) == partyBefore.Members.Single(m => m.CharacterId == f.B.CharacterId),
            "Positive-control admission changed disconnect state or the remaining party.");
        var settlement = await f.Settle();
        Require(settlement.CharacterIds.Order().SequenceEqual(new[] { f.A.CharacterId, f.B.CharacterId }.Order()),
            "Cancelling an unconsumed attempt revoked member reward eligibility.");
        Require(moduleHash == HashFile(module) && sourceHashes.All(x => HashFile(Path.Combine(projectRoot, x.Key)) == x.Value),
            "Selected inputs/module changed during cancellation restart integration.");
        await File.WriteAllTextAsync(Path.Combine(root, "proof.json"), JsonSerializer.Serialize(new {
            schemaVersion = 1, scenario = "CancelUnconsumed", provider = postgres ? "Postgres" : "InMemory",
            editorModuleSha256 = moduleHash, sourceHashes, backendFixtureSha256 = HashFile(typeof(UnrealConsumptionRecoveryTests).Assembly.Location),
            seed, sealLost, resumed, consumptionRequestId = id, observedAtUtc = observed, cancellation.CancelledAt,
            nativeConsumeCalls = 1, faults.ConsumeCalls, faults.RejectedConsume, faults.EvidenceCalls, faults.SealCalls, faults.DroppedSeal,
            faults.DisconnectionCalls, lateConsumesDenied = 4, freshAdmissionGeneration = 1,
            scope = new { actualHttp = true, actualPostgres = postgres, separateUnrealProcesses = true, sealAckLoss = true,
                originalJournalPreservedAcrossLostSeal = true, immutableCancellationProof = true, noReadmissionDuringRecovery = true,
                noSyntheticGenerationOrDisconnection = true, remainingPartyAndRewardEligibilityPreserved = true,
                lateSameOtherAndLegacyConsumeFenced = true, sealedNonceCannotConsumeNewTicket = true, freshTicketNonceAllowed = true,
                simulatedOriginalPeerLoss = true, actualIrisClients = false, osCrash = false, actualSteam = false }
        }, Json));
        Console.WriteLine($"PASS {(postgres ? "Postgres" : "InMemory")} UE unconsumed cancellation HTTP restart: {Path.Combine(root, "proof.json")}");
    }

    private sealed record PhaseResult(string Phase, int ProcessId, string LogPath, string LogSha256, int ConditionFailedLines);
    private static async Task<PhaseResult> RunUnreal(string phase, string? ticket, Dictionary<string, string> environment,
        string editor, string project, string root, int timeout)
    {
        string log = Path.Combine(root, phase + ".log");
        ProcessStartInfo start = new(editor) { WorkingDirectory = Path.GetDirectoryName(project)!, UseShellExecute = false,
            CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string arg in new[] { project, "/Engine/Maps/Entry", $"-ExecCmds=Automation RunTests {TestPath}",
            "-TestExit=Automation Test Queue Empty", "-unattended", "-nop4", "-nosplash", "-nosound", "-nullrhi", "-nosteam", "-DisablePython",
            "-NoLiveCoding", "-DDC-ForceMemoryCache", "-RPGItemE2EServer", "-RPGConsumeHttpRecovery", $"-abslog={log}",
            $"-UserDir={Path.Combine(root, phase + "-User")}",
            "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" }) start.ArgumentList.Add(arg);
        foreach (var entry in environment) start.Environment[entry.Key] = entry.Value;
        start.Environment["PROJECT_RPG_RECOVERY_TEST_PHASE"] = phase;
        if (ticket is null) start.Environment.Remove("PROJECT_RPG_RECOVERY_TEST_TICKET");
        else start.Environment["PROJECT_RPG_RECOVERY_TEST_TICKET"] = ticket;
        using Process child = Process.Start(start) ?? throw new TestFailure("Unable to start owned UE integration process.");
        int pid = child.Id;
        Console.WriteLine($"Started owned UE recovery phase {phase}, PID {pid}: {log}");
        Task<string> stdout = child.StandardOutput.ReadToEndAsync(), stderr = child.StandardError.ReadToEndAsync();
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(timeout));
        try { await child.WaitForExitAsync(deadline.Token); }
        finally
        {
            if (!child.HasExited) { child.Kill(entireProcessTree: true); await child.WaitForExitAsync(); }
            await File.WriteAllTextAsync(Path.Combine(root, phase + ".console.log"), await stdout + await stderr);
        }
        Require(child.ExitCode == 0 && File.Exists(log), $"UE {phase} failed; inspect {log}.");
        string text = await File.ReadAllTextAsync(log);
        Require(text.Contains($"Test Completed. Result={{Success}} Name={{DungeonConsumeHttpRestart}} Path={{{TestPath}}}", StringComparison.Ordinal)
            && !text.Contains("Result={Fail}", StringComparison.Ordinal) && !text.Contains("Fatal error:", StringComparison.Ordinal)
            && !text.Contains("Ensure condition failed:", StringComparison.Ordinal), $"UE {phase} did not pass its integration assertions: {log}.");
        string marker = phase switch {
            "Seed" => "DUNGEON_CONSUME_HTTP_SEEDED", "SealLost" => "DUNGEON_CONSUME_HTTP_SEAL_LOST",
            _ => "DUNGEON_CONSUME_HTTP_RESUMED"
        };
        Require(text.Contains(marker, StringComparison.Ordinal), $"UE {phase} omitted its recovery milestone.");
        return new(phase, pid, log, HashFile(log), text.Split('\n').Count(line => line.Contains("Condition failed", StringComparison.Ordinal)));
    }
    private static string HashFile(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));
}
