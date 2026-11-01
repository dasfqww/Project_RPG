using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

try
{
    if (args.Contains("--departure-catalog-recovery-only"))
    {
        try { await DungeonDepartureCatalogRecoveryTests.Run(); }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"FAIL departure catalog: {exception.GetType().Name}: {exception.Message}");
            return 1;
        }
        await DungeonDepartureTests.Run("InMemory", new InMemoryGameRepository());
        return 0;
    }
    if (args.Contains("--party-presentation-only"))
    {
        DungeonPartyPresentationTests.Run();
        return 0;
    }
    if (args.Contains("--departure-only"))
    {
        await DungeonDepartureTests.Run("InMemory", new InMemoryGameRepository());
        string? departurePostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(departurePostgres))
        {
            await using PrivateSchema departureSchema = await PrivateSchema.Create(departurePostgres);
            await DungeonDepartureTests.Run("Postgres", departureSchema.Repository());
            await DungeonDepartureTests.CheckPersistence(departureSchema);
        }
        else if (args.Contains("--require-postgres")) return 2;
        else Console.WriteLine("SKIP PostgreSQL departure: no local test database configured.");
        return 0;
    }
    if (args.Contains("--content-catalog-only"))
    {
        DungeonContentCatalogTests.CheckPolicy();
        if (!args.Contains("--postgres-only"))
            await DungeonContentCatalogTests.RunSuite("InMemory", new InMemoryGameRepository());
        string? contentPostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(contentPostgres))
        {
            await using PrivateSchema contentSchema = await PrivateSchema.Create(contentPostgres);
            await DungeonContentCatalogTests.RunSuite("Postgres", contentSchema.Repository());
        }
        else if (args.Contains("--require-postgres")) return 2;
        else Console.WriteLine("SKIP PostgreSQL content claim: no local test database configured.");
        return 0;
    }
    if (args.Contains("--ue-consumption-recovery"))
    {
        await UnrealConsumptionRecoveryTests.Run(args);
        return 0;
    }
    if (args.Contains("--consumption-seal-only"))
    {
        if (!args.Contains("--postgres-only"))
            await JoinTicketConsumptionSealTests.RunSuite("InMemory", clock => new InMemoryGameRepository(clock));
        string? sealPostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(sealPostgres))
        {
            await using PrivateSchema sealSchema = await PrivateSchema.Create(sealPostgres);
            await JoinTicketConsumptionSealTests.RunSuite("Postgres", clock => sealSchema.Repository(clock));
            await JoinTicketConsumptionSealTests.CheckPostgresTransactions(sealSchema);
        }
        else if (args.Contains("--require-postgres")) return 2;
        else Console.WriteLine("SKIP PostgreSQL consumption seal: no local test database configured.");
        return 0;
    }
    if (args.Contains("--consumption-evidence-only"))
    {
        if (!args.Contains("--postgres-only"))
            await JoinTicketConsumptionEvidenceTests.RunSuite("InMemory", clock => new InMemoryGameRepository(clock));
        string? evidencePostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(evidencePostgres))
        {
            await using PrivateSchema evidenceSchema = await PrivateSchema.Create(evidencePostgres);
            await JoinTicketConsumptionEvidenceTests.RunSuite("Postgres", clock => evidenceSchema.Repository(clock));
            await JoinTicketConsumptionEvidenceTests.CheckPostgresPersistenceAndLock(evidenceSchema);
        }
        else if (args.Contains("--require-postgres")) return 2;
        else Console.WriteLine("SKIP PostgreSQL consumption evidence: no local test database configured.");
        return 0;
    }
    if (args.Contains("--join-ticket-consumption-only"))
    {
        if (!args.Contains("--postgres-only"))
            await JoinTicketConsumptionTests.RunSuite("InMemory", clock => new InMemoryGameRepository(clock));
        string? consumptionPostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(consumptionPostgres))
        {
            await using PrivateSchema consumptionSchema = await PrivateSchema.Create(consumptionPostgres);
            await JoinTicketConsumptionTests.RunSuite("Postgres", clock => consumptionSchema.Repository(clock));
            await JoinTicketConsumptionTests.CheckPostgresAtomicity(consumptionSchema);
        }
        else if (args.Contains("--require-postgres")) return 2;
        else Console.WriteLine("SKIP PostgreSQL consumption recovery: no local test database configured.");
        return 0;
    }
    if (args.Contains("--server-startup-lease-only"))
    {
        ServerStartupLeaseTests.CheckPolicy();
        await ServerStartupLeaseTests.RunSuite("InMemory", () => new InMemoryGameRepository());
        string? startupPostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(startupPostgres))
        {
            await using PrivateSchema startupSchema = await PrivateSchema.Create(startupPostgres);
            await ServerStartupLeaseTests.RunSuite("Postgres", () => startupSchema.Repository());
        }
        else if (args.Contains("--require-postgres")) return 2;
        else Console.WriteLine("SKIP PostgreSQL startup lease: no local test database configured.");
        return 0;
    }
    if (args.Contains("--disconnection-observation-only"))
    {
        await DisconnectionObservationTests.RunSuite("InMemory", clock => new InMemoryGameRepository(clock));
        await Tests.CheckAdmissionHttp(await Fixture.Create(new InMemoryGameRepository()));
        string? observationPostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(observationPostgres))
        {
            await using PrivateSchema schema = await PrivateSchema.Create(observationPostgres);
            await DisconnectionObservationTests.RunSuite("Postgres", clock => schema.Repository(clock));
            await Tests.CheckAdmissionHttp(await Fixture.Create(schema.Repository()));
        }
        else if (args.Contains("--require-postgres")) return 2;
        else Console.WriteLine("SKIP PostgreSQL observation: no local test database configured.");
        return 0;
    }
    if (args.Contains("--logout-only"))
    {
        await PlayerLogoutTests.RunSuite("InMemory", new InMemoryGameRepository());
        string? logoutPostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(logoutPostgres))
        {
            await using PrivateSchema logoutSchema = await PrivateSchema.Create(logoutPostgres);
            await PlayerLogoutTests.RunSuite("Postgres", logoutSchema.Repository());
        }
        else
        {
            Console.WriteLine("SKIP PostgreSQL logout: no local test database configured.");
            if (args.Contains("--require-postgres")) return 2;
        }
        return 0;
    }
    await JoinTicketIssuanceTests.RunSuite("InMemory", clock => new InMemoryGameRepository(clock));
    await JoinTicketIssuanceTests.CheckMemoryLock();
    if (args.Contains("--join-ticket-issuance-only"))
    {
        string? ticketPostgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
        if (!string.IsNullOrWhiteSpace(ticketPostgres))
        {
            await using PrivateSchema ticketSchema = await PrivateSchema.Create(ticketPostgres);
            await JoinTicketIssuanceTests.RunSuite("Postgres", clock => ticketSchema.Repository(clock));
            await JoinTicketIssuanceTests.CheckPostgresLock(ticketSchema);
        }
        else
        {
            Console.WriteLine("SKIP PostgreSQL ticket issuance: no local test database configured.");
            if (args.Contains("--require-postgres")) return 2;
        }
        return 0;
    }
    bool connectionRewardsOnly = args.Contains("--connection-rewards-only");
    if (!connectionRewardsOnly)
    {
        await Tests.RunSuite("InMemory", () => new InMemoryGameRepository());
        await MembershipTests.RunSuite("InMemory", () => new InMemoryGameRepository());
    }
    await ConnectionRewardTests.RunSuite("InMemory", clock => new InMemoryGameRepository(clock));
    await ConnectionRewardTests.CheckMemoryLock();
    await ConnectionRewardTests.CheckMemoryAdmissionLock();
    await ConnectionRewardTests.CheckMemoryTicketLock();
    await ConnectionExpirationTests.RunSuite("InMemory", clock => new InMemoryGameRepository(clock));
    await ConnectionExpirationTests.CheckMemoryLock();
    string? postgres = Environment.GetEnvironmentVariable("PROJECT_RPG_ABANDON_TEST_POSTGRES");
    if (string.IsNullOrWhiteSpace(postgres))
    {
        Console.WriteLine("SKIP PostgreSQL: set PROJECT_RPG_ABANDON_TEST_POSTGRES to a loopback development database.");
        if (args.Contains("--require-postgres")) return 2;
    }
    else
    {
        if (!connectionRewardsOnly)
        {
            await using PrivateSchema schema = await PrivateSchema.Create(postgres);
            await Tests.RunSuite("Postgres", () => schema.Repository());
            await Tests.CheckPersistenceAndRollback(schema);
            // Keep worker claims isolated from earlier pending settlement jobs.
            await using PrivateSchema membershipSchema = await PrivateSchema.Create(postgres);
            await MembershipTests.RunSuite("Postgres", () => membershipSchema.Repository());
        }
        await using PrivateSchema connectionSchema = await PrivateSchema.Create(postgres);
        await JoinTicketIssuanceTests.RunSuite("Postgres", clock => connectionSchema.Repository(clock));
        await JoinTicketIssuanceTests.CheckPostgresLock(connectionSchema);
        await ConnectionRewardTests.RunSuite("Postgres", clock => connectionSchema.Repository(clock));
        await ConnectionRewardTests.CheckPostgresLockAndPersistence(connectionSchema);
        await ConnectionRewardTests.CheckPostgresAdmissionLock(connectionSchema);
        await ConnectionRewardTests.CheckPostgresTicketLock(connectionSchema);
        await ConnectionExpirationTests.RunSuite("Postgres", clock => connectionSchema.Repository(clock));
        await ConnectionExpirationTests.CheckPostgresLock(connectionSchema);
        await ConnectionExpirationTests.CheckPostgresReceiptRollback(connectionSchema);
    }
    return 0;
}
catch (Exception exception)
{
    // Never print credentials, bearer tokens or database connection strings.
    Console.Error.WriteLine("FAIL Abandonment: " + (exception is TestFailure ? exception.Message
        : exception is PostgresException database ? "SQLSTATE " + database.SqlState : exception.GetType().Name));
    return 1;
}

static class Tests
{
    public static void Require(bool condition, string message)
    {
        if (!condition) throw new TestFailure(message);
    }

    private static async Task Reject<T>(Func<Task> action) where T : Exception
    {
        try { await action(); } catch (T) { return; }
        throw new TestFailure("Expected rejection: " + typeof(T).Name);
    }

    public static async Task RunSuite(string name, Func<IGameRepository> create)
    {
        int passed = 0;
        async Task Test(string label, Func<Fixture, Task> action, DungeonSessionState state = DungeonSessionState.InProgress)
        {
            Fixture fixture = await Fixture.Create(create(), state);
            await action(fixture);
            Console.WriteLine($"PASS {name}: {label}");
            passed++;
        }

        await Test("unexpected-disconnect marker keeps the first server time and party intact", async f =>
        {
            ConsumedJoinTicket firstAdmission = await f.Admit();
            Require(firstAdmission.ConnectionGeneration == 1, "First admission has no connection generation.");
            DateTimeOffset first = await f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId,
                firstAdmission.ConnectionGeneration, f.Now, default);
            DateTimeOffset repeated = await f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId,
                firstAdmission.ConnectionGeneration, f.Now.AddMinutes(9), default);
            Require(first == f.Now && repeated == first, "Duplicate disconnect extended the grace clock.");
            ConsumedJoinTicket secondAdmission = await f.Admit();
            Require(secondAdmission.ConnectionGeneration == firstAdmission.ConnectionGeneration + 1,
                "A new ticket did not advance the connection generation.");
            await Reject<DungeonSessionMembershipException>(() => f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId,
                firstAdmission.ConnectionGeneration, f.Now.AddMinutes(9), default));
            DungeonSession session = await f.Read();
            Require(session.Members.Count == 2
                && session.Members.Single(member => member.CharacterId == f.A.CharacterId).DisconnectedAt == first
                && session.Members.Single(member => member.CharacterId == f.A.CharacterId).ConnectionGeneration
                    == secondAdmission.ConnectionGeneration
                && session.Members.Single(member => member.CharacterId == f.B.CharacterId).DisconnectedAt is null
                && session.ExpiresAt == f.Expires && session.State == DungeonSessionState.InProgress,
                "Disconnect marker altered another member or the session lease.");
            await Reject<DungeonSessionServerMismatchException>(() => f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, "wrong-server", f.A.SteamId, f.A.CharacterId,
                secondAdmission.ConnectionGeneration, f.Now, default));
            await Reject<DungeonSessionMembershipException>(() => f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, f.Server, f.B.SteamId, f.A.CharacterId,
                secondAdmission.ConnectionGeneration, f.Now, default));
            await f.Settle();
            await Reject<DungeonSessionStateConflictException>(() => f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId,
                secondAdmission.ConnectionGeneration, f.Now, default));
        });

        await Test("same-generation disconnect before confirmation cannot be cleared by a late commit", async f =>
        {
            ConsumedJoinTicket first = await f.Admit();
            DateTimeOffset firstAt = await f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId,
                first.ConnectionGeneration, f.Now, default);
            Require(!await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server,
                f.A.SteamId, f.A.CharacterId, first.ConnectionGeneration,
                f.Now.AddSeconds(1), default),
                "Late confirmation revived a socket already reported disconnected.");
            DungeonSession afterFirst = await f.Read();
            Require(afterFirst.Members.Single(m => m.CharacterId == f.A.CharacterId) is
                    { DisconnectedAt: var at, DisconnectedGeneration: var generation }
                && at == firstAt && generation == first.ConnectionGeneration,
                "Early report did not retain its terminal generation.");

            ConsumedJoinTicket second = await f.Admit();
            Require(await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server,
                f.A.SteamId, f.A.CharacterId, second.ConnectionGeneration,
                f.Now.AddMinutes(1), default) == firstAt,
                "Failed reentry extended the original grace clock.");
            Require(!await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server,
                f.A.SteamId, f.A.CharacterId, second.ConnectionGeneration,
                f.Now.AddMinutes(1), default),
                "A second lost socket was revived by its late confirmation.");
            DungeonSession afterSecond = await f.Read();
            Require(afterSecond.Members.Single(m => m.CharacterId == f.A.CharacterId) is
                    { DisconnectedAt: var original, DisconnectedGeneration: var latest }
                && original == firstAt && latest == second.ConnectionGeneration,
                "Second report did not preserve first time and current generation.");

            ConsumedJoinTicket third = await f.Admit();
            Require(await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server,
                f.A.SteamId, f.A.CharacterId, third.ConnectionGeneration,
                f.Now.AddMinutes(9), default),
                "A genuinely new admission before deadline could not cancel grace.");
            DungeonSession recovered = await f.Read();
            Require(recovered.Members.Single(m => m.CharacterId == f.A.CharacterId) is
                    { DisconnectedAt: null, DisconnectedGeneration: 0 },
                "Successful newer admission retained the old disconnect tombstone.");
        });

        await Test("simultaneous confirmation and socket-loss report always retain disconnection", async _ =>
        {
            for (int i = 0; i < 8; i++)
            {
                Fixture f = await Fixture.Create(create());
                ConsumedJoinTicket admission = await f.Admit();
                TaskCompletionSource start = new(TaskCreationOptions.RunContinuationsAsynchronously);
                Task report = Task.Run(async () =>
                {
                    await start.Task;
                    await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server,
                        f.A.SteamId, f.A.CharacterId, admission.ConnectionGeneration, f.Now, default);
                });
                Task confirm = Task.Run(async () =>
                {
                    await start.Task;
                    await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server,
                        f.A.SteamId, f.A.CharacterId, admission.ConnectionGeneration, f.Now, default);
                });
                start.SetResult();
                await Task.WhenAll(report, confirm);
                DungeonSession current = await f.Read();
                Require(current.Members.Single(m => m.CharacterId == f.A.CharacterId) is
                        { DisconnectedAt: var at, DisconnectedGeneration: var generation }
                    && at == f.Now && generation == admission.ConnectionGeneration,
                    "A concurrent late confirmation erased the socket-loss report.");
            }
        });

        await Test("successful admission alone clears grace; replay cannot clear a later disconnect", async f =>
        {
            ConsumedJoinTicket first = await f.Admit();
            await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, first.ConnectionGeneration, f.Now, default);
            ConsumedJoinTicket second = await f.Admit();
            Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).DisconnectedAt == f.Now,
                "Ticket consumption cleared grace before UE admission.");
            await Reject<DungeonSessionMembershipException>(() => f.Repo.ConfirmDungeonMemberAdmissionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId, first.ConnectionGeneration,
                f.Now.AddMinutes(9), default));
            Require(await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, second.ConnectionGeneration, f.Now.AddMinutes(10).AddTicks(-10), default),
                "Admission immediately before the ten-minute deadline was rejected.");
            Require(await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, second.ConnectionGeneration, f.Now.AddMinutes(10), default),
                "Connected admission retry was not idempotent.");
            DateTimeOffset nextDisconnect = f.Now.AddMinutes(11);
            await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, second.ConnectionGeneration, nextDisconnect, default);
            Require(!await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, second.ConnectionGeneration, nextDisconnect.AddMinutes(1), default),
                "Replayed confirmation cleared a later disconnect on the same connection.");
            DungeonSession session = await f.Read();
            Require(session.Members.Single(m => m.CharacterId == f.A.CharacterId) is
                    { DisconnectedAt: var recorded, ConfirmedConnectionGeneration: var confirmed }
                && recorded == nextDisconnect && confirmed == second.ConnectionGeneration
                && session.Members.Single(m => m.CharacterId == f.B.CharacterId).DisconnectedAt is null
                && session.ExpiresAt == f.Expires, "Admission confirmation changed another member or lease.");
        });

        await Test("ten-minute deadline rejects admission even before an eviction worker runs", async f =>
        {
            ConsumedJoinTicket first = await f.Admit();
            await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, first.ConnectionGeneration, f.Now, default);
            ConsumedJoinTicket second = await f.Admit();
            Require(!await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, second.ConnectionGeneration, f.Now.AddMinutes(10), default),
                "Admission at the exact deadline was accepted.");
            Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).DisconnectedAt == f.Now,
                "Expired admission cleared disconnect state.");
            await Reject<DungeonSessionServerMismatchException>(() => f.Repo.ConfirmDungeonMemberAdmissionAsync(
                f.Session, "wrong-server", f.A.SteamId, f.A.CharacterId,
                second.ConnectionGeneration, f.Now, default));
            await Reject<DungeonSessionMembershipException>(() => f.Repo.ConfirmDungeonMemberAdmissionAsync(
                f.Session, f.Server, f.B.SteamId, f.A.CharacterId,
                second.ConnectionGeneration, f.Now, default));
            await f.Settle();
            await Reject<DungeonSessionStateConflictException>(() => f.Repo.ConfirmDungeonMemberAdmissionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId,
                second.ConnectionGeneration, f.Now, default));
        });

        await Test("only the caller leaves; remaining party and server continue", async f =>
        {
            var receipt = await f.Abandon();
            var current = await f.Read();
            Require(receipt.SessionStateAtAbandonment == DungeonSessionState.InProgress, "Wrong receipt state.");
            Require(current.State == DungeonSessionState.InProgress && current.ServerId == f.Server
                && current.ServerAddress == "127.0.0.1:7777" && current.Members.Count == 1
                && current.Members[0].CharacterId == f.B.CharacterId && current.ExpiresAt == f.Expires, "Party changed.");
            Require(!await f.Repo.IsActiveDungeonSessionMemberAsync(f.Session, f.A.SteamId, f.A.CharacterId, f.Now, default), "Abandoned member active.");
            Require(!await f.Repo.IsAuthorizedGameServerSessionMemberAsync(f.Session, f.Server, f.A.SteamId, f.A.CharacterId, f.Now, default), "Old server authority survived.");
            Require(await f.Repo.IsActiveDungeonSessionMemberAsync(f.Session, f.B.SteamId, f.B.CharacterId, f.Now, default), "Remaining member lost lease.");
            await f.Repo.HeartbeatDungeonSessionAsync(f.Session, f.Server, f.Now, f.Expires, default);
            Require((await f.Read()).Members.Count == 1, "Heartbeat restored abandoned member.");
        });
        await Test("only remaining members enter the immutable reward snapshot", async f =>
        {
            await f.Abandon();
            var reward = await f.Settle();
            Require(reward.CharacterIds.SequenceEqual([f.B.CharacterId]), "Abandoned character included in rewards.");
            var receipt = await f.Abandon(); // a successful opt-out can be retried after settlement
            Require(receipt.SessionStateAtAbandonment == DungeonSessionState.InProgress, "Retry became current session state.");
            Require((await f.Read()).State == DungeonSessionState.SettlementPending, "Retry changed settlement.");
        });
        await Test("last opt-out closes the session without settlement", async f =>
        {
            await f.Abandon();
            var last = await f.Abandon(f.B);
            Require(last.SessionStateAtAbandonment == DungeonSessionState.Closed, "Last member did not close.");
            Require((await f.Read()) is { State: DungeonSessionState.Closed, Members.Count: 0 }, "Closed party retained members.");
            Require(await f.Repo.GetActiveDungeonSessionForCharacterAsync(f.B.CharacterId, f.B.SteamId, f.Now, default) is null, "Closed party retained lease.");
            Require(await f.Abandon(f.B) == last, "Closed receipt not idempotent.");
            await Reject<DungeonSessionStateConflictException>(() => f.Settle());
            await Reject<DungeonSessionStateConflictException>(() => f.Repo.HeartbeatDungeonSessionAsync(f.Session, f.Server, f.Now, f.Expires, default));
        });
        await Test("old retry cannot release a newer session lease", async f =>
        {
            var first = await f.Abandon();
            DungeonSession next = await f.Repo.CreateDungeonSessionAsync(f.A.SteamId, f.A.CharacterId, "Dungeon.Next", "Hard", f.Now, f.Expires, default);
            Require(await f.Abandon() == first, "Retry acknowledgement changed.");
            var current = await f.Repo.GetActiveDungeonSessionForCharacterAsync(f.A.CharacterId, f.A.SteamId, f.Now, default);
            Require(current?.DungeonSessionId == next.DungeonSessionId, "Retry released new lease.");
            await Reject<CharacterSessionConflictException>(() => f.Repo.CreateDungeonSessionAsync(f.A.SteamId, f.A.CharacterId, "Dungeon.Other", "Hard", f.Now, f.Expires, default));
        });
        await Test("loading member can opt out without cancelling the others", async f =>
        {
            await f.Abandon();
            Require((await f.Read()).State == DungeonSessionState.Loading, "Loading party cancelled.");
            await f.Repo.StartDungeonSessionAsync(f.Session, f.Server, f.Now, f.Expires, default);
            Require((await f.Read()).Members.Single().CharacterId == f.B.CharacterId, "Start restored member.");
        }, DungeonSessionState.Loading);
        await Test("waiting leave remains separate and does not forfeit rewards", async f =>
        {
            await Reject<DungeonSessionStateConflictException>(() => f.Abandon());
            await f.Repo.LeaveDungeonSessionAsync(f.Session, f.A.SteamId, f.A.CharacterId, f.Now, default);
            Require((await f.Read()) is { State: DungeonSessionState.Waiting, Members.Count: 1 }, "Waiting leave changed.");
        }, DungeonSessionState.Waiting);
        await Test("settlement-first rejects opt-out and preserves every recipient", async f =>
        {
            var snapshot = await f.Settle();
            await Reject<DungeonSessionStateConflictException>(() => f.Abandon());
            Require(snapshot.CharacterIds.Order().SequenceEqual(new[] { f.A.CharacterId, f.B.CharacterId }.Order()), "Snapshot corrupted.");
            Require((await f.Read()).Members.Count == 2, "Pending settlement member removed.");
            Require((await f.Settle()).CharacterIds.SequenceEqual(snapshot.CharacterIds), "Settlement retry changed recipients.");
        });
        foreach (var outcome in new[] { DungeonSessionState.Cleared, DungeonSessionState.Failed })
            await Test($"{outcome} cannot become a new abandonment", async f =>
            {
                await f.Repo.FinishDungeonSessionAsync(f.Session, f.Server, outcome, f.Now, default);
                await Reject<DungeonSessionStateConflictException>(() => f.Abandon());
                Require((await f.Read()).Members.Count == 2, "Terminal membership mutated.");
            });
        await Test("expired session cannot create an opt-out receipt", async f =>
        {
            await Reject<DungeonSessionStateConflictException>(() => f.Repo.AbandonDungeonSessionAsync(f.Session, f.A.SteamId, f.A.CharacterId, f.Expires, default));
            // PostgreSQL rolls back cleanup with the rejected mutation. Query at
            // the same expiry boundary, not the fixture's earlier creation clock.
            var expired = await f.Repo.GetDungeonSessionAsync(f.Session, f.Expires, default);
            Require(expired is { State: DungeonSessionState.Closed, Members.Count: 2 }, "Expiry boundary or rejection membership changed.");
        });
        await Test("wrong account, nonmember and unknown session cannot opt out", async f =>
        {
            await Reject<DungeonSessionMembershipException>(() => f.Repo.AbandonDungeonSessionAsync(f.Session, f.B.SteamId, f.A.CharacterId, f.Now, default));
            await Reject<DungeonSessionMembershipException>(() => f.Abandon(f.C));
            await Reject<DungeonSessionNotFoundException>(() => f.Repo.AbandonDungeonSessionAsync(Guid.NewGuid(), f.A.SteamId, f.A.CharacterId, f.Now, default));
            Require((await f.Read()).Members.Count == 2, "Rejection changed membership.");
            await f.Abandon();
            await Reject<DungeonSessionMembershipException>(() => f.Repo.AbandonDungeonSessionAsync(f.Session, f.B.SteamId, f.A.CharacterId, f.Now, default));
        });
        await Test("cancelled request does not remove membership", async f =>
        {
            using CancellationTokenSource cancelled = new(); cancelled.Cancel();
            await Reject<OperationCanceledException>(() => f.Repo.AbandonDungeonSessionAsync(f.Session, f.A.SteamId, f.A.CharacterId, f.Now, cancelled.Token));
            Require((await f.Read()).Members.Count == 2, "Cancellation changed membership.");
        });
        await Test("abandoned character cannot rejoin or use old/late tickets", async f =>
        {
            string old = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
            string other = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
            await f.Ticket(old, f.A); await f.Ticket(other, f.B); await f.Abandon();
            Require(await f.Consume(old) is null, "Old ticket admitted abandoned member.");
            await f.Ticket(old, f.A); // simulate a request whose pre-check happened before opt-out
            Require(await f.Consume(old) is null, "Late ticket restored membership.");
            Require((await f.Consume(other))?.CharacterId == f.B.CharacterId, "Other member ticket invalidated.");
            await Reject<DungeonSessionNotJoinableException>(() => f.Repo.JoinDungeonSessionAsync(f.Session, f.A.SteamId, f.A.CharacterId, 4, f.Now, f.Expires, default));
        });
        await Test("sixteen simultaneous retries produce one immutable receipt", async f =>
        {
            var receipts = await Task.WhenAll(Enumerable.Range(0, 16).Select(_ => Task.Run(() => f.Abandon())));
            Require(receipts.All(receipt => receipt == receipts[0]), "Concurrent receipts differ.");
            Require((await f.Read()).Members.Single().CharacterId == f.B.CharacterId, "Concurrent retries changed other member.");
        });
        await Test("simultaneous party opt-out closes exactly once", async f =>
        {
            var receipts = await Task.WhenAll(Task.Run(() => f.Abandon()), Task.Run(() => f.Abandon(f.B)));
            Require(receipts.Count(receipt => receipt.SessionStateAtAbandonment == DungeonSessionState.Closed) == 1, "Close receipt ambiguous.");
            Require((await f.Read()) is { State: DungeonSessionState.Closed, Members.Count: 0 }, "Parallel opt-out left session running.");
        });
        await Test("twenty opt-out/settlement races keep one coherent recipient snapshot", async _ =>
        {
            for (int i = 0; i < 20; i++)
            {
                Fixture f = await Fixture.Create(create());
                TaskCompletionSource start = new(TaskCreationOptions.RunContinuationsAsynchronously);
                var optOut = Task.Run(async () => { await start.Task; try { await f.Abandon(); return true; } catch (DungeonSessionStateConflictException) { return false; } });
                var settle = Task.Run(async () => { await start.Task; return await f.Settle(); });
                start.SetResult();
                await Task.WhenAll(optOut, settle);
                var expected = optOut.Result ? new[] { f.B.CharacterId } : new[] { f.A.CharacterId, f.B.CharacterId };
                Require(settle.Result.CharacterIds.Order().SequenceEqual(expected.Order()), "Opt-out/settlement race inconsistent.");
                Require((await f.Read()).Members.Select(member => member.CharacterId).Order().SequenceEqual(expected.Order()), "Race membership differs from snapshot.");
            }
        });
        await Test("twenty ticket/opt-out races never admit after committed removal", async _ =>
        {
            for (int i = 0; i < 20; i++)
            {
                Fixture f = await Fixture.Create(create());
                string ticket = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
                await f.Ticket(ticket, f.A);
                await Task.WhenAll(Task.Run(() => f.Consume(ticket)), Task.Run(() => f.Abandon()));
                await f.Ticket(ticket, f.A);
                Require(await f.Consume(ticket) is null, "Admission restored removed member.");
            }
        });
        await Test("twenty last-member/settlement races never create empty rewards", async _ =>
        {
            for (int i = 0; i < 20; i++)
            {
                Fixture f = await Fixture.Create(create());
                await f.Abandon();
                TaskCompletionSource start = new(TaskCreationOptions.RunContinuationsAsynchronously);
                var optOut = Task.Run(async () => { await start.Task; try { await f.Abandon(f.B); return true; } catch (DungeonSessionStateConflictException) { return false; } });
                var settle = Task.Run(async () => { await start.Task; try { return await f.Settle(); } catch (DungeonSessionStateConflictException) { return null; } });
                start.SetResult(); await Task.WhenAll(optOut, settle);
                var current = await f.Read();
                if (optOut.Result)
                    Require(settle.Result is null && current is { State: DungeonSessionState.Closed, Members.Count: 0 }, "All-abandon race still settled.");
                else
                    Require(settle.Result?.CharacterIds.SequenceEqual([f.B.CharacterId]) == true
                        && current is { State: DungeonSessionState.SettlementPending, Members.Count: 1 }, "Settlement lost last recipient.");
            }
        });
        await Test("real HTTP bearer authentication, owner-only mutation and replay", CheckHttp);
        await Test("real HTTP server-only admission, disconnection and replay", CheckAdmissionHttp);
        Console.WriteLine($"PASS {name} abandonment suite: {passed} scenarios (including connection-order races).");
    }

    private static async Task CheckHttp(Fixture f)
    {
        TimeProvider clock = new FixedClock(f.Now);
        AccessTokenService tokens = new(clock);
        var player = tokens.Issue(TimeSpan.FromHours(1));
        var foreign = tokens.Issue(TimeSpan.FromHours(1));
        var server = tokens.Issue(TimeSpan.FromHours(1));
        var expired = tokens.Issue(TimeSpan.FromSeconds(-1));
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        await f.Repo.StoreSessionAsync(foreign.TokenHash, f.B.SteamId, foreign.ExpiresAt, default);
        await f.Repo.StoreSessionAsync(expired.TokenHash, f.A.SteamId, expired.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        string admin = tokens.Issue(TimeSpan.FromHours(1)).Token;
        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo);
        builder.Services.AddSingleton(clock); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapDungeonAbandonmentApi();
        await app.StartAsync();
        using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
        string path = $"/api/dungeon-sessions/{f.Session:D}/members/{f.A.CharacterId:D}/abandon";
        async Task<HttpResponseMessage> Send(string? token, string target = "")
        {
            using HttpRequestMessage request = new(HttpMethod.Post, target.Length == 0 ? path : target);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            return await client.SendAsync(request);
        }
        foreach (var (token, code) in new[] { ((string?)null, 401), ("invalid", 401), (expired.Token, 401), (foreign.Token, 403), (server.Token, 403), (admin, 403) })
        {
            using var response = await Send(token);
            Require((int)response.StatusCode == code, "HTTP principal boundary failed.");
        }
        Require((await f.Read()).Members.Count == 2, "Rejected HTTP request changed party.");
        var unjoined = await f.Repo.CreateCharacterAsync(f.A.SteamId, "U" + Guid.NewGuid().ToString("N")[..18], f.Now, default);
        using var nonmember = await Send(player.Token, path.Replace(f.A.CharacterId.ToString("D"), unjoined.CharacterId.ToString("D")));
        Require(nonmember.StatusCode == HttpStatusCode.Forbidden, "Own but unjoined character allowed opt-out.");
        using var first = await Send(player.Token);
        Require(first.StatusCode == HttpStatusCode.OK && first.Headers.CacheControl?.NoStore == true, "Opt-out HTTP acknowledgement missing.");
        var receipt = await first.Content.ReadFromJsonAsync<DungeonAbandonmentResponse>();
        Require(receipt is { RewardExcluded: true, SessionStateAtAbandonment: "InProgress" }
            && receipt.CharacterId == f.A.CharacterId && receipt.DungeonSessionId == f.Session, "HTTP receipt mismatched.");
        await f.Settle();
        using var restrictedServer = await Send(server.Token);
        Require(restrictedServer.StatusCode == HttpStatusCode.Forbidden, "Post-session credential allowed opt-out.");
        using var replay = await Send(player.Token);
        Require(replay.StatusCode == HttpStatusCode.OK && await replay.Content.ReadFromJsonAsync<DungeonAbandonmentResponse>() == receipt, "HTTP replay changed receipt.");
        using var pending = await Send(foreign.Token, path.Replace(f.A.CharacterId.ToString("D"), f.B.CharacterId.ToString("D")));
        Require(pending.StatusCode == HttpStatusCode.Conflict, "HTTP opt-out changed pending rewards.");
        using var missing = await Send(player.Token, path.Replace(f.Session.ToString("D"), Guid.NewGuid().ToString("D")));
        Require(missing.StatusCode == HttpStatusCode.NotFound, "Unknown session response wrong.");
        await app.StopAsync();
    }

    public static async Task CheckAdmissionHttp(Fixture f)
    {
        ConsumedJoinTicket first = await f.Admit();
        await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, f.A.SteamId,
            f.A.CharacterId, first.ConnectionGeneration, f.Now.AddMinutes(-9), default);
        ConsumedJoinTicket second = await f.Admit();
        FixedClock clock = new(f.Now);
        AccessTokenService tokens = new(clock);
        var player = tokens.Issue(TimeSpan.FromHours(1));
        var server = tokens.Issue(TimeSpan.FromHours(1));
        var otherServer = tokens.Issue(TimeSpan.FromHours(1));
        Fixture foreignSession = await Fixture.Create(f.Repo);
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(otherServer.TokenHash, foreignSession.Server, foreignSession.Session,
            otherServer.ExpiresAt, default);
        WebApplicationBuilder builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo);
        builder.Services.AddSingleton<TimeProvider>(clock); builder.Services.AddSingleton(tokens);
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using WebApplication app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapDungeonAdmissionApi();
        await app.StartAsync();
        using HttpClient client = new() { BaseAddress = new Uri(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
        string path = $"/api/dungeon-sessions/{f.Session:D}/members/{f.A.CharacterId:D}/admission-confirmation";
        async Task<HttpResponseMessage> Send(string? token, string steamId, long generation, string? target = null)
        {
            using HttpRequestMessage request = new(HttpMethod.Post, target ?? path);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            request.Content = JsonContent.Create(new ConfirmDungeonAdmissionRequest(steamId, generation));
            return await client.SendAsync(request);
        }
        string disconnectPath = path.Replace("/admission-confirmation", "/disconnection", StringComparison.Ordinal);
        async Task<HttpResponseMessage> Report(string? token, string steamId, long generation,
            string? target = null, DateTimeOffset? observedAtUtc = null)
        {
            using HttpRequestMessage request = new(HttpMethod.Post, target ?? disconnectPath);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            request.Content = JsonContent.Create(new RecordDungeonDisconnectionRequest(steamId, generation, observedAtUtc));
            return await client.SendAsync(request);
        }
        using (var anonymous = await Send(null, f.A.SteamId, second.ConnectionGeneration))
            Require(anonymous.StatusCode == HttpStatusCode.Unauthorized, "Anonymous admission confirmation was accepted.");
        using (var user = await Send(player.Token, f.A.SteamId, second.ConnectionGeneration))
            Require(user.StatusCode == HttpStatusCode.Forbidden, "Player token confirmed server admission.");
        using (var foreign = await Send(otherServer.Token, f.A.SteamId, second.ConnectionGeneration))
            Require(foreign.StatusCode == HttpStatusCode.Forbidden, "Foreign server confirmed admission.");
        using (var wrongOwner = await Send(server.Token, f.B.SteamId, second.ConnectionGeneration))
            Require(wrongOwner.StatusCode == HttpStatusCode.Forbidden, "Server confirmed a different owner.");
        using (var oldGeneration = await Send(server.Token, f.A.SteamId, first.ConnectionGeneration))
            Require(oldGeneration.StatusCode == HttpStatusCode.Forbidden, "Server confirmed a stale generation.");
        using (var good = await Send(server.Token, f.A.SteamId, second.ConnectionGeneration))
            Require(good.StatusCode == HttpStatusCode.OK && good.Headers.CacheControl?.NoStore == true,
                "Valid server confirmation failed.");
        Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).DisconnectedAt is null,
            "HTTP confirmation did not clear grace.");
        clock.Set(f.Now.AddMinutes(9));
        using (var anonymous = await Report(null, f.A.SteamId, second.ConnectionGeneration))
            Require(anonymous.StatusCode == HttpStatusCode.Unauthorized, "Anonymous disconnect was accepted.");
        using (var user = await Report(player.Token, f.A.SteamId, second.ConnectionGeneration))
            Require(user.StatusCode == HttpStatusCode.Forbidden, "Player token reported a disconnect.");
        using (var foreign = await Report(otherServer.Token, f.A.SteamId, second.ConnectionGeneration))
            Require(foreign.StatusCode == HttpStatusCode.Forbidden, "Foreign server reported a disconnect.");
        using (var wrongOwner = await Report(server.Token, f.B.SteamId, second.ConnectionGeneration))
            Require(wrongOwner.StatusCode == HttpStatusCode.Forbidden, "Server disconnected another owner.");
        using (var oldGeneration = await Report(server.Token, f.A.SteamId, first.ConnectionGeneration))
            Require(oldGeneration.StatusCode == HttpStatusCode.Forbidden, "Stale socket disconnected current admission.");
        using (var invalid = await Report(server.Token, f.A.SteamId, 0))
            Require(invalid.StatusCode == HttpStatusCode.BadRequest, "Zero generation was accepted.");
        using (var invalidClock = await Report(server.Token, f.A.SteamId, second.ConnectionGeneration,
            observedAtUtc: clock.GetUtcNow().AddSeconds(6)))
            Require(invalidClock.StatusCode == HttpStatusCode.BadRequest, "Invalid observation passed HTTP binding.");
        using (var firstReport = await Report(server.Token, f.A.SteamId, second.ConnectionGeneration, observedAtUtc: f.Now))
        {
            var response = await firstReport.Content.ReadFromJsonAsync<Dictionary<string, DateTimeOffset>>();
            Require(firstReport.StatusCode == HttpStatusCode.OK
                && firstReport.Headers.CacheControl?.NoStore == true
                && response?.GetValueOrDefault("disconnectedAt") == f.Now,
                "Server disconnect report replaced the observation with its later receipt time.");
        }
        using (var duplicate = await Report(server.Token, f.A.SteamId, second.ConnectionGeneration))
        {
            var response = await duplicate.Content.ReadFromJsonAsync<Dictionary<string, DateTimeOffset>>();
            Require(duplicate.StatusCode == HttpStatusCode.OK
                && response?.GetValueOrDefault("disconnectedAt") == f.Now,
                "Duplicate disconnect report moved the grace start.");
        }
        using (var replay = await Send(server.Token, f.A.SteamId, second.ConnectionGeneration))
            Require(replay.StatusCode == HttpStatusCode.Conflict, "HTTP replay cleared a later disconnect.");
        Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).DisconnectedAt == f.Now,
            "HTTP replay altered disconnect marker.");
        clock.Set(f.Now); // Keep the separate post-settlement credential check within its short TTL.
        await f.Settle();
        using (var completed = await Report(server.Token, f.A.SteamId, second.ConnectionGeneration))
            Require(completed.StatusCode == HttpStatusCode.Forbidden,
                "Post-settlement telemetry-only credential reported a disconnect.");
        await app.StopAsync();
        Console.WriteLine("PASS real HTTP: server-only delayed observation, clock rejection and immutable replay.");
    }

    public static async Task CheckPersistenceAndRollback(PrivateSchema schema)
    {
        Fixture disconnected = await Fixture.Create(schema.Repository());
        ConsumedJoinTicket admission = await disconnected.Admit();
        await disconnected.Repo.RecordDungeonMemberDisconnectionAsync(
            disconnected.Session, disconnected.Server, disconnected.A.SteamId,
            disconnected.A.CharacterId, admission.ConnectionGeneration, disconnected.Now, default);
        DungeonSession restored = (await schema.Repository().GetDungeonSessionAsync(
            disconnected.Session, disconnected.Now, default))!;
        Require(restored.Members.Single(member => member.CharacterId == disconnected.A.CharacterId).DisconnectedAt
            == disconnected.Now
            && restored.Members.Single(member => member.CharacterId == disconnected.A.CharacterId).ConnectionGeneration
                == admission.ConnectionGeneration
            && restored.Members.Single(member => member.CharacterId == disconnected.A.CharacterId).DisconnectedGeneration
                == admission.ConnectionGeneration,
            "Disconnect time/generation did not survive a fresh repository connection.");
        Console.WriteLine("PASS Postgres: unexpected-disconnect marker survives a fresh repository connection.");
        ConsumedJoinTicket next = await disconnected.Admit();
        Require(await disconnected.Repo.ConfirmDungeonMemberAdmissionAsync(disconnected.Session, disconnected.Server,
            disconnected.A.SteamId, disconnected.A.CharacterId, next.ConnectionGeneration,
            disconnected.Now.AddMinutes(9), default), "Persisted disconnect could not be cleared by valid admission.");
        DungeonSession confirmed = (await schema.Repository().GetDungeonSessionAsync(
            disconnected.Session, disconnected.Now, default))!;
        Require(confirmed.Members.Single(member => member.CharacterId == disconnected.A.CharacterId) is
                { DisconnectedAt: null, ConfirmedConnectionGeneration: var generation, DisconnectedGeneration: 0 }
            && generation == next.ConnectionGeneration,
            "Admission confirmation did not survive a fresh repository connection.");
        Console.WriteLine("PASS Postgres: confirmed admission survives a fresh repository connection.");
        Fixture f = await Fixture.Create(schema.Repository());
        var first = await f.Abandon();
        Require(await schema.Repository().AbandonDungeonSessionAsync(f.Session, f.A.SteamId, f.A.CharacterId, f.Now, default) == first, "Receipt did not survive repository recreation.");
        Console.WriteLine("PASS Postgres: receipt survives a fresh repository and pooled connection.");
        Fixture rollback = await Fixture.Create(schema.Repository());
        string ticket = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
        await rollback.Ticket(ticket, rollback.A);
        // Trusted UUID from this fixture. Only the generated private schema is affected.
        await schema.Execute($"ALTER TABLE dungeon_session_abandonments ADD CONSTRAINT reject_fixture_receipt CHECK (character_id <> '{rollback.A.CharacterId:D}'::uuid);");
        try { await Reject<PostgresException>(() => rollback.Abandon()); }
        finally { await schema.Execute("ALTER TABLE dungeon_session_abandonments DROP CONSTRAINT reject_fixture_receipt;"); }
        Require((await rollback.Read()).Members.Count == 2, "Failed receipt insert lost membership.");
        Require(await rollback.Repo.IsActiveDungeonSessionMemberAsync(rollback.Session, rollback.A.SteamId, rollback.A.CharacterId, rollback.Now, default), "Rollback lost lease.");
        Require((await rollback.Consume(ticket))?.CharacterId == rollback.A.CharacterId, "Rollback lost ticket.");
        await rollback.Abandon();
        Console.WriteLine("PASS Postgres: receipt failure rolls back membership, lease, tickets and state; retry recovers.");
        await schema.CheckAdmissionLock();
        Console.WriteLine("PASS Postgres: admission waits for the shared session-row lock before validating a ticket.");
        await schema.CheckAdmissionAfterAbandonment();
        Console.WriteLine("PASS Postgres: admission queued behind abandonment rejects the removed member after commit.");
        await schema.CheckMembershipLock();
        Console.WriteLine("PASS Postgres: membership reads wait for the shared session-row lock.");
    }
}

sealed class Fixture(IGameRepository repo, DateTimeOffset now, GameCharacter a, GameCharacter b, GameCharacter c, Guid session, string server)
{
    public IGameRepository Repo => repo;
    public DateTimeOffset Now => now;
    public DateTimeOffset Expires => now.AddHours(1);
    public GameCharacter A => a;
    public GameCharacter B => b;
    public GameCharacter C => c;
    public Guid Session => session;
    public string Server => server;
    public Task<DungeonAbandonment> Abandon(GameCharacter? member = null) => repo.AbandonDungeonSessionAsync(session, (member ?? a).SteamId, (member ?? a).CharacterId, now, default);
    public async Task<DungeonSession> Read() => (await repo.GetDungeonSessionAsync(session, now, default))!;
    public Task<DungeonRewardSettlement> Settle() => repo.EnqueueDungeonRewardSettlementAsync(session, server, "abandon_v1", new string('a', 64), [new CurrencyChange("TestGold", 100)], [], now, default);
    public Task Ticket(string hash, GameCharacter member) => repo.StoreJoinTicketAsync(hash, member.SteamId, member.CharacterId, session, Expires, default);
    public Task<ConsumedJoinTicket?> Consume(string hash) => repo.ConsumeJoinTicketAsync(hash, server, session, now, default);
    public async Task<ConsumedJoinTicket> Admit(GameCharacter? member = null)
    {
        string ticket = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
        await Ticket(ticket, member ?? a);
        return await Consume(ticket) ?? throw new TestFailure("Fixture admission failed.");
    }

    public static async Task<Fixture> Create(IGameRepository repo, DungeonSessionState state = DungeonSessionState.InProgress)
    {
        DateTimeOffset now = new(DateTime.UtcNow.Ticks / 10 * 10, TimeSpan.Zero);
        async Task<GameCharacter> Character()
        {
            string steam = BitConverter.ToUInt64(Guid.NewGuid().ToByteArray()).ToString("D20");
            await repo.UpsertAccountAsync(new(steam, steam, false, false), now, default);
            return await repo.CreateCharacterAsync(steam, "A" + Guid.NewGuid().ToString("N")[..18], now, default);
        }
        GameCharacter a = await Character(), b = await Character(), c = await Character();
        var session = await repo.CreateDungeonSessionAsync(a.SteamId, a.CharacterId, "Dungeon.Test", "Hard", now, now.AddHours(1), default);
        await repo.JoinDungeonSessionAsync(session.DungeonSessionId, b.SteamId, b.CharacterId, 4, now, now.AddHours(1), default);
        string server = "abandon-" + Guid.NewGuid().ToString("N");
        if (state != DungeonSessionState.Waiting)
            await repo.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:7777", now, now.AddHours(1), default);
        if (state == DungeonSessionState.InProgress)
            await repo.StartDungeonSessionAsync(session.DungeonSessionId, server, now, now.AddHours(1), default);
        return new(repo, now, a, b, c, session.DungeonSessionId, server);
    }
}

sealed class FixedClock(DateTimeOffset now) : TimeProvider
{
    public void Set(DateTimeOffset value) => now = value;
    public override DateTimeOffset GetUtcNow() => now;
}
sealed class TestFailure(string message) : Exception(message);

sealed class PrivateSchema : IAsyncDisposable
{
    private readonly NpgsqlDataSource admin;
    private readonly NpgsqlDataSource source;
    private readonly string name = "rpg_abandon_test_" + Guid.NewGuid().ToString("N");
    private bool created;
    private PrivateSchema(NpgsqlConnectionStringBuilder configuration)
    {
        configuration.SearchPath = "pg_catalog";
        admin = NpgsqlDataSource.Create(configuration.ConnectionString);
        configuration.SearchPath = name;
        configuration.ApplicationName = name;
        source = NpgsqlDataSource.Create(configuration.ConnectionString);
    }
    public NpgsqlDataSource Source => source;
    public PostgresGameRepository Repository(TimeProvider? settlementClock = null) => new(source, NullLogger<PostgresGameRepository>.Instance, settlementClock);
    public async Task Execute(string sql) { await using var command = source.CreateCommand(sql); await command.ExecuteNonQueryAsync(); }
    public async Task CheckAdmissionLock()
    {
        Fixture f = await Fixture.Create(Repository());
        string ticket = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
        await f.Ticket(ticket, f.A);
        await using var connection = await source.OpenConnectionAsync();
        await using var transaction = await connection.BeginTransactionAsync();
        await using (var command = new NpgsqlCommand("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id = @id FOR UPDATE;", connection, transaction))
        {
            command.Parameters.AddWithValue("id", f.Session);
            await command.ExecuteScalarAsync();
        }
        Task<ConsumedJoinTicket?> consume = f.Consume(ticket);
        bool committed = false;
        try
        {
            bool observed = false;
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                await using var blocked = source.CreateCommand("SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE application_name = @name AND wait_event_type = 'Lock' AND query LIKE '%FOR UPDATE%');");
                blocked.Parameters.AddWithValue("name", name);
                if (await blocked.ExecuteScalarAsync() is true) { observed = true; break; }
                await Task.Delay(20);
            }
            Tests.Require(observed && !consume.IsCompleted, "Admission did not wait for the parent session lock.");
            await transaction.CommitAsync();
            committed = true;
            Tests.Require((await consume)?.CharacterId == f.A.CharacterId, "Admission failed after lock release.");
        }
        finally
        {
            if (!committed) await transaction.RollbackAsync();
            await consume;
        }
    }
    public async Task CheckAdmissionAfterAbandonment()
    {
        Fixture f = await Fixture.Create(Repository());
        string ticket = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
        await f.Ticket(ticket, f.A);
        await using var connection = await source.OpenConnectionAsync();
        await using var transaction = await connection.BeginTransactionAsync();
        await using (var command = new NpgsqlCommand("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id = @id FOR UPDATE;", connection, transaction))
        {
            command.Parameters.AddWithValue("id", f.Session);
            await command.ExecuteScalarAsync();
        }

        Task<DungeonAbandonment>? abandon = null;
        Task<ConsumedJoinTicket?>? consume = null;
        bool committed = false;
        try
        {
            async Task WaitForBlockedOperations(int expected)
            {
                DateTime deadline = DateTime.UtcNow.AddSeconds(5);
                while (DateTime.UtcNow < deadline)
                {
                    await using var blocked = source.CreateCommand("SELECT COUNT(*) FROM pg_stat_activity WHERE application_name = @name AND wait_event_type = 'Lock' AND query LIKE '%FOR UPDATE%';");
                    blocked.Parameters.AddWithValue("name", name);
                    if ((long)(await blocked.ExecuteScalarAsync())! >= expected) return;
                    await Task.Delay(20);
                }
                throw new TestFailure($"Expected {expected} operations waiting for the session lock.");
            }

            // Queue the real repository abandonment first, then admission. The
            // held row lock makes this ordering observable rather than timing-based.
            abandon = f.Abandon();
            await WaitForBlockedOperations(1);
            consume = f.Consume(ticket);
            await WaitForBlockedOperations(2);
            Tests.Require(!abandon.IsCompleted && !consume.IsCompleted, "A queued operation bypassed the session lock.");
            await transaction.CommitAsync();
            committed = true;
            DungeonAbandonment receipt = await abandon;
            Tests.Require(await consume is null, "Admission accepted a ticket after abandonment committed.");
            DungeonSession current = await f.Read();
            Tests.Require(receipt.CharacterId == f.A.CharacterId
                && current.Members.Count == 1 && current.Members[0].CharacterId == f.B.CharacterId,
                "Abandonment/admission ordering changed the remaining party.");
        }
        finally
        {
            if (!committed) await transaction.RollbackAsync();
            if (abandon is not null) await abandon;
            if (consume is not null) await consume;
        }
    }
    public async Task CheckMembershipLock()
    {
        Fixture f = await Fixture.Create(Repository());
        await using var connection = await source.OpenConnectionAsync();
        await using var transaction = await connection.BeginTransactionAsync();
        await using (var command = new NpgsqlCommand("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id = @id FOR UPDATE;", connection, transaction))
        {
            command.Parameters.AddWithValue("id", f.Session);
            await command.ExecuteScalarAsync();
        }
        Task<DungeonMembershipSnapshot?> read = f.Repo.GetDungeonMembershipSnapshotAsync(f.Session, f.Server, default);
        bool committed = false;
        try
        {
            bool observed = false;
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                await using var blocked = source.CreateCommand("SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE application_name = @name AND wait_event_type = 'Lock' AND query LIKE '%FOR UPDATE%');");
                blocked.Parameters.AddWithValue("name", name);
                if (await blocked.ExecuteScalarAsync() is true) { observed = true; break; }
                await Task.Delay(20);
            }
            Tests.Require(observed && !read.IsCompleted, "Membership read did not wait for the parent session lock.");
            await transaction.CommitAsync();
            committed = true;
            Tests.Require((await read)?.Members.Count == 2, "Membership read failed after lock release.");
        }
        finally
        {
            if (!committed) await transaction.RollbackAsync();
            await read;
        }
    }
    public static async Task<PrivateSchema> Create(string connectionString)
    {
        var configuration = new NpgsqlConnectionStringBuilder(connectionString) { Timeout = 5, CommandTimeout = 30, MaxPoolSize = 16 };
        Tests.Require(configuration.Host is "127.0.0.1" or "localhost" or "::1", "PostgreSQL tests require loopback.");
        PrivateSchema fixture = new(configuration);
        try
        {
            await using var command = fixture.admin.CreateCommand($"CREATE SCHEMA \"{fixture.name}\";");
            await command.ExecuteNonQueryAsync(); fixture.created = true;
            await PostgresSchemaInitializer.ApplyAsync(fixture.source, await PostgresSchemaMigrations.LoadInitializationSqlAsync(AppContext.BaseDirectory, default), default);
            Console.WriteLine("Fixture: " + fixture.name + " (public data untouched)");
            return fixture;
        }
        catch { await fixture.DisposeAsync(); throw; }
    }
    public async ValueTask DisposeAsync()
    {
        await source.DisposeAsync();
        try
        {
            if (!created) return;
            Tests.Require(Regex.IsMatch(name, "^rpg_abandon_test_[a-f0-9]{32}$"), "Unsafe schema cleanup target.");
            await using var owner = admin.CreateCommand("SELECT nspowner = (SELECT oid FROM pg_roles WHERE rolname = current_user) FROM pg_namespace WHERE nspname = @name;");
            owner.Parameters.AddWithValue("name", name);
            Tests.Require(await owner.ExecuteScalarAsync() is true, "Fixture ownership changed; cleanup refused.");
            await using var cleanup = admin.CreateCommand($"DROP SCHEMA \"{name}\" CASCADE;");
            await cleanup.ExecuteNonQueryAsync(); created = false;
            Console.WriteLine("Fixture cleaned: " + name);
        }
        finally { await admin.DisposeAsync(); }
    }
}
