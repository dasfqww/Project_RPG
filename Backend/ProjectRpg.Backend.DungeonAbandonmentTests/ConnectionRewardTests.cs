using System.Collections.Concurrent;
using System.Reflection;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Tests;

static class ConnectionRewardTests
{
    private sealed class Clock : TimeProvider
    {
        private long ticks = DateTimeOffset.UtcNow.UtcTicks;
        public void Set(DateTimeOffset now) => Interlocked.Exchange(ref ticks, now.UtcTicks);
        public override DateTimeOffset GetUtcNow() => new(Interlocked.Read(ref ticks), TimeSpan.Zero);
    }

    // Observe the real worker's payout batch; do not run currency/item writes
    // while asserting who is eligible for the immutable settlement snapshot.
    private sealed class CommitRecorder : IDungeonRewardCommitRepository
    {
        public ConcurrentQueue<DungeonRewardCommitBatch> Batches { get; } = new();
        public Task<DungeonRewardCommitResult> CommitAsync(DungeonRewardCommitBatch batch,
            DateTimeOffset at, CancellationToken cancellation)
        {
            cancellation.ThrowIfCancellationRequested(); Batches.Enqueue(batch);
            return Task.FromResult(new DungeonRewardCommitResult(true, null));
        }
    }

    private static Task<DungeonRewardSettlement> Settle(Fixture f, DateTimeOffset submittedAt)
        => f.Repo.EnqueueDungeonRewardSettlementAsync(f.Session, f.Server, "connection_reward_v1",
            new string('c', 64), [new CurrencyChange("TestGold", 100)], [], submittedAt, default);

    private static async Task<ConsumedJoinTicket> Disconnect(Fixture f, GameCharacter? member = null,
        DateTimeOffset? at = null)
    {
        member ??= f.A;
        ConsumedJoinTicket admission = await f.Admit(member);
        await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server, member.SteamId,
            member.CharacterId, admission.ConnectionGeneration, at ?? f.Now, default);
        return admission;
    }

    public static async Task RunSuite(string provider, Func<TimeProvider, IGameRepository> create)
    {
        int count = 0;
        async Task Test(string name, Func<Fixture, Clock, Task> action)
        {
            Clock clock = new(); Fixture f = await Fixture.Create(create(clock)); clock.Set(f.Now);
            await action(f, clock); count++; Console.WriteLine($"PASS {provider} connection reward: {name}");
        }

        // These are first: the PostgreSQL worker's ClaimNext is schema-wide and
        // must not accidentally claim pending snapshots from later test cases.
        await Test("all expired members produce a zero-recipient completed worker batch", async (f, clock) =>
        {
            await Disconnect(f); await Disconnect(f, f.B); clock.Set(f.Now.AddMinutes(10));
            DungeonRewardSettlement settlement = await Settle(f, f.Now);
            Require(settlement.CharacterIds.Count == 0, "Expired party received a payout snapshot.");
            await CheckWorker(f, clock, []);
            Require((await f.Read()).Members.Count == 2, "Reward filtering removed membership.");
        });
        await Test("the worker builds payout entries for the remaining member only", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(10));
            DungeonRewardSettlement settlement = await Settle(f, f.Now);
            Require(settlement.CharacterIds.SequenceEqual([f.B.CharacterId]), "Remaining member not isolated.");
            await CheckWorker(f, clock, [f.B.CharacterId]);
        });
        await Test("absence of a server disconnect marker keeps both members eligible", async (f, clock) =>
        {
            clock.Set(f.Now.AddMinutes(20)); DungeonRewardSettlement settlement = await Settle(f, f.Now);
            Require(settlement.CharacterIds.Order().SequenceEqual(new[] { f.A.CharacterId, f.B.CharacterId }.Order()),
                "An ordinary member without a disconnect marker lost reward eligibility.");
        });
        await Test("one microsecond before ten minutes remains eligible", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(10).AddTicks(-10));
            Require((await Settle(f, f.Now)).CharacterIds.Count == 2, "Grace ended before its exact deadline.");
        });
        await Test("exact deadline excludes only the disconnected member and keeps membership/leases", async (f, clock) =>
        {
            await Disconnect(f); DungeonSession before = await f.Read(); clock.Set(f.Now.AddMinutes(10));
            DungeonRewardSettlement settlement = await Settle(f, f.Now);
            DungeonSession after = await f.Read();
            Require(settlement.CharacterIds.SequenceEqual([f.B.CharacterId]) && after.Members.SequenceEqual(before.Members),
                "Expired member paid or remaining party/membership/leases changed.");
            Require(settlement.CreatedAt == f.Now.AddMinutes(10), "Snapshot reused stale pre-lock request time.");
        });
        await Test("duplicate disconnect reports do not extend reward eligibility", async (f, clock) =>
        {
            ConsumedJoinTicket admission = await Disconnect(f);
            DateTimeOffset first = await f.Repo.RecordDungeonMemberDisconnectionAsync(f.Session, f.Server,
                f.A.SteamId, f.A.CharacterId, admission.ConnectionGeneration, f.Now.AddMinutes(9), default);
            clock.Set(f.Now.AddMinutes(10));
            Require(first == f.Now && !(await Settle(f, f.Now)).CharacterIds.Contains(f.A.CharacterId),
                "A duplicate report extended the reward deadline.");
        });
        await Test("ticket consumption alone does not restore reward eligibility", async (f, clock) =>
        {
            await Disconnect(f); await f.Admit(); clock.Set(f.Now.AddMinutes(10));
            Require(!(await Settle(f, f.Now)).CharacterIds.Contains(f.A.CharacterId),
                "Unconfirmed ticket consumption cleared the disconnect deadline.");
        });
        await Test("ticket consumption closes at the exact grace deadline without affecting the party", async (f, clock) =>
        {
            await Disconnect(f);
            string beforeTicket = Guid.NewGuid().ToString("N");
            await f.Ticket(beforeTicket, f.A);
            clock.Set(f.Now.AddMinutes(10).AddTicks(-10));
            ConsumedJoinTicket before = await f.Consume(beforeTicket)
                ?? throw new TestFailure("A reconnect just before the deadline was rejected.");

            string expiredTicket = Guid.NewGuid().ToString("N");
            await f.Ticket(expiredTicket, f.A);
            clock.Set(f.Now.AddMinutes(10));
            Require(await f.Consume(expiredTicket) is null, "An expired member consumed a new ticket.");
            DungeonSession session = await f.Read();
            Require(session.Members.Single(member => member.CharacterId == f.A.CharacterId) is
                    { DisconnectedAt: var disconnectedAt, ConnectionGeneration: var generation }
                && disconnectedAt == f.Now && generation == before.ConnectionGeneration
                && (await f.Admit(f.B)).CharacterId == f.B.CharacterId
                && (await f.Read()).Members.Count == 2,
                "An expired admission changed the member generation, marker or remaining party.");
        });
        await Test("confirmed recovery before expiry permits later ticket consumption", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
            ConsumedJoinTicket recovered = await f.Admit();
            Require(await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server,
                f.A.SteamId, f.A.CharacterId, recovered.ConnectionGeneration, f.Now.AddMinutes(9), default),
                "In-grace admission confirmation failed.");
            clock.Set(f.Now.AddMinutes(20));
            Require((await f.Admit()).ConnectionGeneration == recovered.ConnectionGeneration + 1,
                "A recovered member was incorrectly blocked after the old deadline.");
        });
        await Test("confirmed reconnection before the deadline restores eligibility", async (f, clock) =>
        {
            await Disconnect(f); ConsumedJoinTicket next = await f.Admit();
            Require(await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server, f.A.SteamId,
                f.A.CharacterId, next.ConnectionGeneration, f.Now.AddMinutes(9), default), "Reconnection failed.");
            clock.Set(f.Now.AddMinutes(20));
            Require((await Settle(f, f.Now)).CharacterIds.Count == 2, "Confirmed reconnect remained excluded.");
        });
        await Test("a snapshot taken before expiry is immutable across later retries", async (f, clock) =>
        {
            await Disconnect(f); clock.Set(f.Now.AddMinutes(9)); DungeonRewardSettlement first = await Settle(f, f.Now);
            clock.Set(f.Now.AddMinutes(20)); DungeonRewardSettlement retry = await Settle(f, f.Now.AddMinutes(20));
            Require(first.CharacterIds.Count == 2 && retry.CharacterIds.SequenceEqual(first.CharacterIds)
                && retry.CreatedAt == first.CreatedAt, "A later retry retroactively removed a captured recipient.");
        });
        await Test("future markers and unrecorded disconnects are not inferred expired", async (f, clock) =>
        {
            await Disconnect(f, at: f.Now.AddMinutes(20)); clock.Set(f.Now.AddMinutes(10));
            Require((await Settle(f, f.Now)).CharacterIds.Count == 2, "A future marker was treated as expired.");
            Require(!DungeonConnectionPolicy.HasReconnectGraceExpired(null, f.Now)
                && !DungeonConnectionPolicy.HasReconnectGraceExpired(DateTimeOffset.MaxValue, DateTimeOffset.MinValue),
                "Unknown/future timestamps overflowed or implied disconnection.");
        });
        Console.WriteLine($"PASS {provider} connection reward: {count} scenarios.");
    }

    private static async Task CheckWorker(Fixture f, Clock clock, Guid[] expectedRecipients)
    {
        CommitRecorder recorder = new();
        using DungeonRewardSettlementWorker worker = new(f.Repo, recorder, clock,
            Options.Create(new DungeonSettlementOptions { PollIntervalMilliseconds = 50 }),
            NullLogger<DungeonRewardSettlementWorker>.Instance);
        await worker.StartAsync(default);
        try
        {
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while ((await f.Read()).State != DungeonSessionState.Cleared && DateTime.UtcNow < deadline)
                await Task.Delay(10);
            Require((await f.Read()).State == DungeonSessionState.Cleared, "Worker did not complete the snapshot.");
            Require(recorder.Batches.TryDequeue(out DungeonRewardCommitBatch? batch) && recorder.Batches.IsEmpty
                && batch.CurrencyEntries.Select(entry => entry.Request.CharacterId).Order().SequenceEqual(expectedRecipients.Order())
                && batch.ItemRequests.Count == 0, "Worker payout batch leaked an excluded recipient.");
        }
        finally { await worker.StopAsync(default); }
    }

    public static async Task CheckMemoryLock()
    {
        Clock clock = new(); InMemoryGameRepository repository = new(clock);
        Fixture f = await Fixture.Create(repository); await Disconnect(f); clock.Set(f.Now.AddMinutes(9));
        object gate = typeof(InMemoryGameRepository).GetField("_dungeonGate", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(repository)!;
        Task<DungeonRewardSettlement> pending;
        Thread? waiting = null;
        lock (gate)
        {
            pending = Task.Run(async () => { waiting = Thread.CurrentThread; return await Settle(f, f.Now); });
            Require(SpinWait.SpinUntil(() => waiting is not null && (waiting.ThreadState & System.Threading.ThreadState.WaitSleepJoin) != 0,
                TimeSpan.FromSeconds(5)), "Memory settlement did not wait on the dungeon gate.");
            clock.Set(f.Now.AddMinutes(10));
        }
        Require((await pending.WaitAsync(TimeSpan.FromSeconds(5))).CharacterIds.SequenceEqual([f.B.CharacterId]),
            "Memory recipient capture used time from before the gate wait.");
        Console.WriteLine("PASS InMemory connection reward: recipient clock read after actual dungeon-gate wait.");
    }

    public static async Task CheckPostgresLockAndPersistence(PrivateSchema schema)
    {
        Clock clock = new(); Fixture f = await Fixture.Create(schema.Repository(clock)); await Disconnect(f);
        clock.Set(f.Now.AddMinutes(9));
        await using NpgsqlConnection connection = await schema.Source.OpenConnectionAsync();
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync();
        await using (NpgsqlCommand hold = new("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id=@id FOR UPDATE;", connection, transaction))
        {
            hold.Parameters.AddWithValue("id", f.Session); await hold.ExecuteScalarAsync();
        }
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
        Task<DungeonRewardSettlement> pending = f.Repo.EnqueueDungeonRewardSettlementAsync(f.Session, f.Server,
            "connection_reward_v1", new string('c', 64), [new CurrencyChange("TestGold", 100)], [], f.Now, timeout.Token);
        bool released = false;
        try
        {
            await RequirePostgresLockWait(schema, pending);
            clock.Set(f.Now.AddMinutes(10)); await transaction.CommitAsync(); released = true;
            DungeonRewardSettlement first = await pending;
            Require(first.CharacterIds.SequenceEqual([f.B.CharacterId]) && first.CreatedAt == f.Now.AddMinutes(10),
                "PostgreSQL eligibility used the pre-lock clock.");
            clock.Set(f.Now.AddMinutes(30));
            DungeonRewardSettlement restored = await schema.Repository(clock).EnqueueDungeonRewardSettlementAsync(f.Session,
                f.Server, first.RewardVersion, first.CommandFingerprint, first.Changes, first.ItemRewards, f.Now.AddMinutes(30), default);
            Require(restored.CharacterIds.SequenceEqual(first.CharacterIds) && restored.CreatedAt == first.CreatedAt,
                "Fresh repository changed frozen recipients.");
            Console.WriteLine("PASS Postgres connection reward: actual parent-row wait crosses deadline; fresh connection preserves recipients.");
        }
        finally
        {
            if (!released) { timeout.Cancel(); await transaction.RollbackAsync(); try { await pending; } catch (OperationCanceledException) { } }
        }
    }

    public static async Task CheckMemoryAdmissionLock()
    {
        Clock clock = new(); InMemoryGameRepository repository = new(clock);
        Fixture f = await Fixture.Create(repository); await Disconnect(f);
        ConsumedJoinTicket next = await f.Admit(); clock.Set(f.Now.AddMinutes(9));
        object gate = typeof(InMemoryGameRepository).GetField("_dungeonGate", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(repository)!;
        Task<bool> pending; Thread? waiting = null;
        lock (gate)
        {
            pending = Task.Run(async () =>
            {
                waiting = Thread.CurrentThread;
                return await f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server,
                    f.A.SteamId, f.A.CharacterId, next.ConnectionGeneration, f.Now.AddMinutes(9), default);
            });
            Require(SpinWait.SpinUntil(() => waiting is not null && (waiting.ThreadState & System.Threading.ThreadState.WaitSleepJoin) != 0,
                TimeSpan.FromSeconds(5)), "Memory admission did not wait for the dungeon gate.");
            clock.Set(f.Now.AddMinutes(10));
        }
        Require(!await pending.WaitAsync(TimeSpan.FromSeconds(5))
            && (await f.Read()).Members.Single(member => member.CharacterId == f.A.CharacterId).DisconnectedAt == f.Now
            && (await Settle(f, f.Now)).CharacterIds.SequenceEqual([f.B.CharacterId]),
            "A stale pre-lock admission clock restored an expired member's reward eligibility.");
        Console.WriteLine("PASS InMemory connection reward: admission waiting past the deadline cannot clear grace or regain rewards.");
    }

    public static async Task CheckMemoryTicketLock()
    {
        Clock clock = new(); InMemoryGameRepository repository = new(clock);
        Fixture f = await Fixture.Create(repository); await Disconnect(f);
        string ticket = Guid.NewGuid().ToString("N"); await f.Ticket(ticket, f.A);
        clock.Set(f.Now.AddMinutes(9));
        object gate = typeof(InMemoryGameRepository).GetField("_dungeonGate", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(repository)!;
        Task<ConsumedJoinTicket?> pending; Thread? waiting = null;
        lock (gate)
        {
            pending = Task.Run(async () => { waiting = Thread.CurrentThread; return await f.Consume(ticket); });
            Require(SpinWait.SpinUntil(() => waiting is not null && (waiting.ThreadState & System.Threading.ThreadState.WaitSleepJoin) != 0,
                TimeSpan.FromSeconds(5)), "Memory ticket consume did not wait on the dungeon gate.");
            clock.Set(f.Now.AddMinutes(10));
        }
        Require(await pending.WaitAsync(TimeSpan.FromSeconds(5)) is null
            && (await f.Read()).Members.Single(member => member.CharacterId == f.A.CharacterId).ConnectionGeneration == 1,
            "A ticket waiting past the deadline was consumed or advanced admission generation.");
        Console.WriteLine("PASS InMemory connection reward: ticket consumption waits for the gate and rejects expiry.");
    }

    public static async Task CheckPostgresAdmissionLock(PrivateSchema schema)
    {
        Clock clock = new(); Fixture f = await Fixture.Create(schema.Repository(clock));
        await Disconnect(f); ConsumedJoinTicket next = await f.Admit(); clock.Set(f.Now.AddMinutes(9));
        await using NpgsqlConnection connection = await schema.Source.OpenConnectionAsync();
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync();
        await using (NpgsqlCommand hold = new("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id=@id FOR UPDATE;", connection, transaction))
        {
            hold.Parameters.AddWithValue("id", f.Session); await hold.ExecuteScalarAsync();
        }
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
        Task<bool> pending = f.Repo.ConfirmDungeonMemberAdmissionAsync(f.Session, f.Server,
            f.A.SteamId, f.A.CharacterId, next.ConnectionGeneration, f.Now.AddMinutes(9), timeout.Token);
        bool released = false;
        try
        {
            await RequirePostgresLockWait(schema, pending);
            clock.Set(f.Now.AddMinutes(10)); await transaction.CommitAsync(); released = true;
            Require(!await pending && (await f.Read()).Members.Single(member => member.CharacterId == f.A.CharacterId).DisconnectedAt == f.Now
                && (await Settle(f, f.Now)).CharacterIds.SequenceEqual([f.B.CharacterId]),
                "Admission after a real PostgreSQL lock wait bypassed the reward deadline.");
            Console.WriteLine("PASS Postgres connection reward: admission crosses deadline during real lock wait; no grace clear or reward recovery.");
        }
        finally
        {
            if (!released) { timeout.Cancel(); await transaction.RollbackAsync(); try { await pending; } catch (OperationCanceledException) { } }
        }
    }

    public static async Task CheckPostgresTicketLock(PrivateSchema schema)
    {
        Clock clock = new(); Fixture f = await Fixture.Create(schema.Repository(clock));
        await Disconnect(f);
        string ticket = Guid.NewGuid().ToString("N"); await f.Ticket(ticket, f.A);
        clock.Set(f.Now.AddMinutes(9));
        await using NpgsqlConnection connection = await schema.Source.OpenConnectionAsync();
        await using NpgsqlTransaction transaction = await connection.BeginTransactionAsync();
        await using (NpgsqlCommand hold = new("SELECT dungeon_session_id FROM dungeon_sessions WHERE dungeon_session_id=@id FOR UPDATE;", connection, transaction))
        {
            hold.Parameters.AddWithValue("id", f.Session); await hold.ExecuteScalarAsync();
        }
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
        Task<ConsumedJoinTicket?> pending = f.Repo.ConsumeJoinTicketAsync(ticket, f.Server, f.Session,
            f.Now.AddMinutes(9), timeout.Token);
        bool released = false;
        try
        {
            await RequirePostgresLockWait(schema, pending);
            clock.Set(f.Now.AddMinutes(10)); await transaction.CommitAsync(); released = true;
            Require(await pending is null
                && (await f.Read()).Members.Single(member => member.CharacterId == f.A.CharacterId).ConnectionGeneration == 1,
                "A PostgreSQL ticket waiting past the deadline was consumed or advanced generation.");
            Console.WriteLine("PASS Postgres connection reward: ticket consumption waits for the row lock and rejects expiry.");
        }
        finally
        {
            if (!released) { timeout.Cancel(); await transaction.RollbackAsync(); try { await pending; } catch (OperationCanceledException) { } }
        }
    }

    private static async Task RequirePostgresLockWait(PrivateSchema schema, Task pending)
    {
        string application = new NpgsqlConnectionStringBuilder(schema.Source.ConnectionString).ApplicationName!;
        bool waiting = false; DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (DateTime.UtcNow < deadline)
        {
            await using var probe = schema.Source.CreateCommand("SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE application_name=@application AND wait_event_type='Lock' AND query LIKE '%FOR UPDATE%');");
            probe.Parameters.AddWithValue("application", application);
            if (await probe.ExecuteScalarAsync() is true) { waiting = true; break; }
            await Task.Delay(10);
        }
        Require(waiting && !pending.IsCompleted, "Operation did not wait for the PostgreSQL session row.");
    }
}
