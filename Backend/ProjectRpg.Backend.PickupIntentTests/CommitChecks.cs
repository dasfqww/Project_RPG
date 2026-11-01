using System.Buffers.Binary;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Checks;

static class CommitChecks
{
    private static async Task NoGrant(Fixture f)
    {
        Require(await f.Items.FindAsync(f.Command.Mutations[0].NewRecord.ItemId, default) is null
            && await f.Items.TryGetCommitResultAsync(f.Command.RequestId, default) is null, "Rejected commit wrote item/receipt.");
    }

    public static async Task Suite(string label, Func<IGameRepository> create, Func<IItemRepository> createItems)
    {
        int passed = 0;
        async Task Test(string name, Func<Fixture, Task> action)
        {
            await action(await Fixture.Create(create(), items: createItems()));
            Console.WriteLine($"PASS {label}Commit: {name}"); passed++;
        }
        await Test("grant, consumption and immutable exact retry", async f =>
        {
            var prepared = (await f.Prepare()).Intent!;
            var first = await f.Commit(); var retry = await f.Commit(); var stored = (await f.Read()).Intent!;
            Require(first.Status == DungeonPickupCommitStatus.Committed && retry.Status == DungeonPickupCommitStatus.AlreadyCommitted
                && stored.ConsumedAt == f.Now && stored.CommandJson == prepared.CommandJson && stored.PreparedAt == prepared.PreparedAt,
                "Atomic grant/consumption/retry failed.");
            Require(first.Receipt!.RequestId == f.Command.RequestId && first.Receipt.Records.Single().Revision == 1
                && first.Receipt.CommittedAt == retry.Receipt!.CommittedAt, "Original receipt changed.");
            Require((await f.Items.FindByOwnerAsync(f.Command.Actor, true, 100, default)).Count == 1
                && (await f.Items.FindByOwnerAsync(new(ItemOwnerType.Character, f.B.CharacterId.ToString("D")), true, 100, default)).Count == 0,
                "Duplicate grant or other-party ownership changed.");
        });
        await Test("missing and invalid reservation never grant", async f =>
        {
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.NotFound, "Missing origin granted.");
            Require((await f.CommitService().CommitAsync(f.Session, f.Server, Guid.Empty, default)).Status == DungeonPickupCommitStatus.InvalidRequest,
                "Zero request allowed.");
            await NoGrant(f);
        });
        await Test("wrong assigned server and cross-session scope expose no receipt", async f =>
        {
            await f.Prepare();
            var wrong = await f.CommitService().CommitAsync(f.Session, "wrong-server", f.Command.RequestId, default);
            Require(wrong.Status == DungeonPickupCommitStatus.Forbidden && wrong.Receipt is null, "Wrong server committed/exposed receipt.");
            var other = await Fixture.Create(f.Repo, items: f.Items);
            var scope = await other.CommitService().CommitAsync(other.Session, other.Server, f.Command.RequestId, default);
            Require(scope.Status == DungeonPickupCommitStatus.NotFound && scope.Receipt is null, "Another session recovered grant authority.");
            await NoGrant(f);
        });
        await Test("opt-out denies prepared grant but preserves remaining party", async f =>
        {
            await f.Prepare(); await f.Abandon();
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.Forbidden, "Opt-out grant accepted."); await NoGrant(f);
            var remaining = Fixture.CommandFor(f.B);
            Require((await f.Prepare(Guid.NewGuid(), remaining)).Status == DungeonPickupIntentStatus.Prepared
                && (await f.CommitService().CommitAsync(f.Session, f.Server, remaining.RequestId, default)).Status == DungeonPickupCommitStatus.Committed,
                "Remaining party's pickup authority was removed.");
        });
        await Test("commit before opt-out stays committed without retry authority", async f =>
        {
            await f.Prepare(); Require((await f.Commit()).Status == DungeonPickupCommitStatus.Committed, "Initial commit failed.");
            await f.Abandon(); var late = await f.Commit();
            Require(late.Status == DungeonPickupCommitStatus.Forbidden && late.Receipt is null
                && await f.Items.FindAsync(f.Command.Mutations[0].NewRecord.ItemId, default) is not null, "Revocation rolled back grant or revived authority.");
        });
        await Test("expiry denies fresh and consumed retries", async f =>
        {
            await f.Prepare();
            var expired = f.CommitService(new FixedClock(f.Expires));
            Require((await expired.CommitAsync(f.Session, f.Server, f.Command.RequestId, default)).Status == DungeonPickupCommitStatus.Forbidden,
                "Expired scope granted."); await NoGrant(f);
            await f.Commit();
            Require((await expired.CommitAsync(f.Session, f.Server, f.Command.RequestId, default)).Status == DungeonPickupCommitStatus.Forbidden,
                "Consumed origin revived expired scope.");
        });
        await Test("settlement blocks prepared commits", async f =>
        {
            await f.Prepare(); await f.Repo.EnqueueDungeonRewardSettlementAsync(f.Session, f.Server, "commit-boundary", new string('a', 64), [], [], f.Now, default);
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.Forbidden, "Settlement accepted pickup."); await NoGrant(f);
        });
        await Test("failed session blocks prepared commits", async f =>
        {
            await f.Prepare(); await f.Repo.FinishDungeonSessionAsync(f.Session, f.Server, DungeonSessionState.Failed, f.Now, default);
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.Forbidden, "Failed session accepted pickup."); await NoGrant(f);
        });
        await Test("loading does not grant gameplay authority", async _ =>
        {
            var loading = await Fixture.Create(create(), false, createItems());
            Require((await loading.Commit()).Status == DungeonPickupCommitStatus.Forbidden, "Loading scope granted."); await NoGrant(loading);
        });
        await Test("simultaneous duplicate commits grant only once", async f =>
        {
            await f.Prepare(); var results = await Task.WhenAll(Enumerable.Range(0, 20).Select(_ => Task.Run(f.Commit)));
            Require(results.Count(r => r.Status == DungeonPickupCommitStatus.Committed) == 1
                && results.Count(r => r.Status == DungeonPickupCommitStatus.AlreadyCommitted) == 19
                && (await f.Items.FindByOwnerAsync(f.Command.Actor, true, 100, default)).Count == 1, "Concurrent duplicate item/receipt.");
        });
        await Test("commit versus opt-out is serial with no late authority", async f =>
        {
            await f.Prepare(); var commit = Task.Run(f.Commit); var abandon = Task.Run(f.Abandon); await Task.WhenAll(commit, abandon);
            var result = await commit; Require(result.Status is DungeonPickupCommitStatus.Committed or DungeonPickupCommitStatus.Forbidden,
                "Commit/opt-out race inconsistent.");
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.Forbidden, "Late grant after opt-out.");
            if (result.Status == DungeonPickupCommitStatus.Forbidden) await NoGrant(f);
            else Require(await f.Items.TryGetCommitResultAsync(f.Command.RequestId, default) is not null, "Winning commit lost its receipt.");
        });
        await Test("commit versus settlement preserves transaction ordering", async f =>
        {
            await f.Prepare(); var commit = Task.Run(f.Commit);
            var settle = Task.Run(() => f.Repo.EnqueueDungeonRewardSettlementAsync(f.Session, f.Server, "commit-race", new string('b', 64), [], [], f.Now, default));
            await Task.WhenAll(commit, settle);
            Require((await commit).Status is DungeonPickupCommitStatus.Committed or DungeonPickupCommitStatus.Forbidden
                && (await f.Commit()).Status == DungeonPickupCommitStatus.Forbidden, "Settlement race re-enabled grant.");
            if ((await commit).Status == DungeonPickupCommitStatus.Forbidden) await NoGrant(f);
        });
        await Test("slot conflict preserves origin and exact retry after freeing slot", async f =>
        {
            var blocker = Fixture.CommandFor(f.A);
            Require((await f.Items.CommitAsync(blocker, f.Now, default)).Status == ItemRepositoryCommitStatus.Committed, "Slot blocker failed.");
            var original = (await f.Prepare()).Intent!;
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.LocationConflict, "Occupied slot granted."); await NoGrant(f);
            Require((await f.Read()).Intent == original && (await f.Prepare(command: Fixture.CommandFor(f.B))).Status == DungeonPickupIntentStatus.Conflict,
                "Known rejection released/rebound frozen origin.");
            var item = (await f.Items.FindAsync(blocker.Mutations[0].NewRecord.ItemId, default))!;
            var move = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Item.Move", "free-original-slot", item.Owner, 0,
                [new(item.Revision, item with { Location = item.Location with { SlotIndex = 1 } })]);
            Require((await f.Items.CommitAsync(move, f.Now, default)).Status == ItemRepositoryCommitStatus.Committed
                && (await f.Commit()).Status == DungeonPickupCommitStatus.Committed, "Exact retry could not use its original slot.");
        });
        await Test("existing item identity cannot be overwritten or consumed", async f =>
        {
            var other = Fixture.CommandFor(f.B); var record = other.Mutations[0].NewRecord;
            other = other with { Mutations = [new(0, record with { State = record.State with { InstanceId = f.Command.Mutations[0].NewRecord.ItemId } })] };
            await f.Items.CommitAsync(other, f.Now, default); var original = (await f.Prepare()).Intent!;
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.RevisionConflict && (await f.Read()).Intent == original
                && await f.Items.TryGetCommitResultAsync(f.Command.RequestId, default) is null, "Item collision overwrote owner/consumed origin.");
        });
        await Test("generic receipt cannot masquerade as atomic consumption even with same body", async _ =>
        {
            foreach (bool changed in new[] { false, true })
            {
                var f = await Fixture.Create(create(), items: createItems()); await f.Prepare();
                var item = f.Command.Mutations[0].NewRecord;
                var generic = changed ? f.Command with { Mutations = [new(0, item with { State = item.State with { GenerationSeed = 99 } })] } : f.Command;
                await f.Items.CommitAsync(generic, f.Now, default);
                var denied = await f.Commit();
                Require(denied.Status == DungeonPickupCommitStatus.IdempotencyConflict && denied.Receipt is null
                    && (await f.Read()).Intent!.ConsumedAt is null, "Generic receipt adopted as atomic origin consumption.");
            }
        });
        await Test("retry uses original receipt without resurrecting moved or consumed item", async f =>
        {
            await f.Prepare(); var first = (await f.Commit()).Receipt!;
            var item = (await f.Items.FindAsync(first.Records[0].ItemId, default))!;
            var move = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Item.Move", "move-after-pickup", item.Owner, 0,
                [new(item.Revision, item with { Location = item.Location with { SlotIndex = 2 } })]);
            await f.Items.CommitAsync(move, f.Now, default);
            Require((await f.Commit()).Receipt!.Records[0].Location.SlotIndex == 0
                && (await f.Items.FindAsync(item.ItemId, default))!.Location.SlotIndex == 2, "Retry rewrote moved item.");
            item = (await f.Items.FindAsync(item.ItemId, default))!;
            var consume = new ItemRepositoryCommitRequest(Guid.NewGuid(), "Item.Consume", "consume-after-pickup", item.Owner, 1,
                [new(item.Revision, item with { LifecycleState = ItemLifecycleState.Consumed, State = item.State with { Quantity = 0 },
                    Location = new(ItemContainerType.Terminal, "", -1) })]);
            await f.Items.CommitAsync(consume, f.Now, default);
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.AlreadyCommitted
                && (await f.Items.FindAsync(item.ItemId, default))!.LifecycleState == ItemLifecycleState.Consumed, "Retry resurrected consumed item.");
        });
        await Test("returned receipt cannot mutate stored retry snapshot", async f =>
        {
            await f.Prepare(); var receipt = (await f.Commit()).Receipt!;
            ((ItemRecord[])receipt.Records)[0] = receipt.Records[0] with { State = receipt.Records[0].State with { GenerationSeed = 999 } };
            Require((await f.Commit()).Receipt!.Records[0].State.GenerationSeed == 7, "Mutable result corrupted stored receipt.");
        });
        await Test("cancelled commit keeps prepared origin and no item writes", async f =>
        {
            await f.Prepare(); using var cancel = new CancellationTokenSource(); cancel.Cancel();
            try { await f.CommitService().CommitAsync(f.Session, f.Server, f.Command.RequestId, cancel.Token); throw new TestFailure("Cancelled commit accepted."); }
            catch (OperationCanceledException) { }
            await NoGrant(f); Require((await f.Read()).Intent!.ConsumedAt is null && (await f.Commit()).Status == DungeonPickupCommitStatus.Committed,
                "Cancellation consumed/unrecoverably damaged origin.");
        });
        await Test("real HTTP commit authorization, empty body, lost-result retry and no-store", Http);
        Console.WriteLine($"PASS {label}Commit total: {passed} focused cases.");
    }

    private static async Task Http(Fixture f)
    {
        TimeProvider clock = new FixedClock(f.Now); var tokens = new AccessTokenService(clock);
        var player = tokens.Issue(TimeSpan.FromHours(1)); var server = tokens.Issue(TimeSpan.FromHours(1));
        string admin = tokens.Issue(TimeSpan.FromHours(1)).Token;
        await f.Repo.StoreSessionAsync(player.TokenHash, f.A.SteamId, player.ExpiresAt, default);
        await f.Repo.StoreGameServerCredentialAsync(server.TokenHash, f.Server, f.Session, server.ExpiresAt, default);
        var builder = WebApplication.CreateBuilder(); builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(f.Repo); builder.Services.AddSingleton(f.Intents); builder.Services.AddSingleton(f.Items);
        builder.Services.AddSingleton(clock); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<DungeonPickupCommitService>();
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions { AdminToken = admin }));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapDungeonPickupIntentApi(); await app.StartAsync();
        using var client = new HttpClient { BaseAddress = new(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
        string path = $"/api/dungeon-sessions/{f.Session:D}/pickup-intents/{f.Command.RequestId:D}/commit";
        async Task<HttpResponseMessage> Send(string? token, string? body = null, bool chunked = false, string? target = null)
        {
            using var request = new HttpRequestMessage(HttpMethod.Post, target ?? path);
            if (token is not null) request.Headers.Authorization = new("Bearer", token);
            if (body is not null) request.Content = new StringContent(body, Encoding.UTF8, "application/json");
            if (chunked) request.Headers.TransferEncodingChunked = true;
            return await client.SendAsync(request);
        }
        await f.Prepare();
        foreach (var (token, status) in new[] { ((string?)null, 401), ("invalid", 401), (player.Token, 403), (admin, 403) })
        { using var response = await Send(token); Require((int)response.StatusCode == status, "HTTP grant auth boundary."); }
        using (var wrong = await Send(server.Token, target: path.Replace(f.Session.ToString("D"), Guid.NewGuid().ToString("D"))))
            Require(wrong.StatusCode == HttpStatusCode.Forbidden, "HTTP session mismatch granted.");
        foreach (bool chunked in new[] { false, true })
        {
            using var body = await Send(server.Token, "{\"actor\":\"another-member\"}", chunked);
            Require(body.StatusCode == HttpStatusCode.BadRequest, "Caller replaced frozen grant body.");
        }
        await NoGrant(f);
        // Real HTTP commit whose first result is discarded; next request must only read its stored receipt.
        using (var first = await Send(server.Token))
            Require(first.StatusCode == HttpStatusCode.OK && first.Headers.CacheControl?.NoStore == true, "HTTP commit failed.");
        using (var retry = await Send(server.Token))
        {
            Require(retry.StatusCode == HttpStatusCode.OK && retry.Headers.CacheControl?.NoStore == true, "HTTP retry failed.");
            using var json = JsonDocument.Parse(await retry.Content.ReadAsStringAsync());
            Require(json.RootElement.GetProperty("status").GetString() == "AlreadyCommitted"
                && json.RootElement.GetProperty("requestId").GetGuid() == f.Command.RequestId, "HTTP retry receipt mismatch.");
        }
        await f.Abandon();
        using (var removed = await Send(server.Token)) Require(removed.StatusCode == HttpStatusCode.Forbidden, "Removed actor reused committed origin.");
        await f.Repo.EnqueueDungeonRewardSettlementAsync(f.Session, f.Server, "commit-http", new string('c', 64), [], [], f.Now, default);
        using (var restricted = await Send(server.Token)) Require(restricted.StatusCode == HttpStatusCode.Forbidden, "Restricted server committed.");
        await app.StopAsync();
    }

    public static async Task Database(PrivateSchema schema)
    {
        async Task<Fixture> Create() => await Fixture.Create(schema.Repository(), items: schema.Items());
        var persistent = await Create(); await persistent.Prepare(); await persistent.Commit();
        var service = new DungeonPickupCommitService(schema.Repository(), schema.Items(), new FixedClock(persistent.Now),
            Microsoft.Extensions.Logging.Abstractions.NullLogger<DungeonPickupCommitService>.Instance);
        Require((await service.CommitAsync(persistent.Session, persistent.Server, persistent.Command.RequestId, default)).Status == DungeonPickupCommitStatus.AlreadyCommitted
            && await schema.Scalar("SELECT count(*) FROM dungeon_pickup_intents WHERE request_id = @id AND consumed_at IS NOT NULL", persistent.Command.RequestId) == 1,
            "Fresh PG connection lost consumed origin.");
        Console.WriteLine("PASS PostgresCommit: fresh connection preserves atomic consumption and original receipt.");

        foreach (string table in new[] { "item_transaction_receipts", "dungeon_pickup_intents" })
        {
            var f = await Create(); await f.Prepare();
            string condition = table == "dungeon_pickup_intents" ? "consumed_at IS NULL" : "false";
            await schema.Execute($"ALTER TABLE {table} ADD CONSTRAINT reject_atomic_fixture CHECK ({condition}) NOT VALID");
            try { Require((await f.Commit()).Status == DungeonPickupCommitStatus.InternalError, "Forced transaction failure ignored."); }
            finally { await schema.Execute($"ALTER TABLE {table} DROP CONSTRAINT reject_atomic_fixture"); }
            await NoGrant(f); Require((await f.Read()).Intent!.ConsumedAt is null, "Failed grant partially consumed origin.");
            Require((await f.Commit()).Status == DungeonPickupCommitStatus.Committed, "Rolled-back origin could not retry.");
            Console.WriteLine($"PASS PostgresCommit: forced {table} failure rolls back item, receipt and consumption.");
        }

        foreach (bool ownerLock in new[] { false, true })
        {
            var f = await Create(); await f.Prepare(); var clock = new MutableClock(f.Now);
            await using var connection = await schema.Source.OpenConnectionAsync(); await using var transaction = await connection.BeginTransactionAsync();
            await using (var gate = new NpgsqlCommand(ownerLock ? "SELECT pg_advisory_xact_lock(@key)"
                : "SELECT 1 FROM dungeon_sessions WHERE dungeon_session_id = @id FOR UPDATE", connection, transaction))
            {
                if (ownerLock) gate.Parameters.AddWithValue("key", BinaryPrimitives.ReadInt64LittleEndian(
                    SHA256.HashData(Encoding.UTF8.GetBytes($"item_owner|{f.Command.Actor.Type}|{f.Command.Actor.OwnerId}"))));
                else gate.Parameters.AddWithValue("id", f.Session);
                await gate.ExecuteScalarAsync();
            }
            var pending = f.CommitService(clock).CommitAsync(f.Session, f.Server, f.Command.RequestId, default);
            await Wait(schema, pending); clock.Now = f.Expires; await transaction.CommitAsync();
            Require((await pending).Status == DungeonPickupCommitStatus.Forbidden, "Lock wait used stale grant authority.");
            await NoGrant(f); Require((await f.Read()).Intent!.ConsumedAt is null, "Expired waiter consumed origin.");
            Console.WriteLine($"PASS PostgresCommit: live expiry after actual {(ownerLock ? "owner advisory" : "parent row")} lock wait.");
        }

        var cancelled = await Create(); await cancelled.Prepare();
        await using (var connection = await schema.Source.OpenConnectionAsync())
        await using (var transaction = await connection.BeginTransactionAsync())
        {
            await using var gate = new NpgsqlCommand("SELECT 1 FROM dungeon_sessions WHERE dungeon_session_id = @id FOR UPDATE", connection, transaction);
            gate.Parameters.AddWithValue("id", cancelled.Session); await gate.ExecuteScalarAsync();
            using var stop = new CancellationTokenSource();
            var pending = cancelled.CommitService().CommitAsync(cancelled.Session, cancelled.Server, cancelled.Command.RequestId, stop.Token);
            await Wait(schema, pending); stop.Cancel();
            try { await pending; throw new TestFailure("Cancelled PG waiter committed."); } catch (OperationCanceledException) { }
            await transaction.CommitAsync();
        }
        await NoGrant(cancelled); Require((await cancelled.Read()).Intent!.ConsumedAt is null, "Cancelled waiter consumed origin.");
        Console.WriteLine("PASS PostgresCommit: cancellation during parent lock wait leaves all grant state untouched.");

        var missing = await Create(); await missing.Prepare(); await missing.Commit();
        // Only this fixture's own request inside the owned private schema; simulate damaged receipt history.
        await using (var erase = schema.Source.CreateCommand("DELETE FROM item_transaction_receipts WHERE request_id = @id"))
        { erase.Parameters.AddWithValue("id", missing.Command.RequestId); Require(await erase.ExecuteNonQueryAsync() == 1, "Fixture receipt not deleted."); }
        Require((await missing.Commit()).Status == DungeonPickupCommitStatus.InternalError
            && await missing.Items.TryGetCommitResultAsync(missing.Command.RequestId, default) is null, "Consumed origin silently recreated missing receipt/grant.");
        Console.WriteLine("PASS PostgresCommit: consumed origin with missing receipt fails closed without regrant.");

        var altered = await Create(); await altered.Prepare(); await altered.Commit();
        await using (var change = schema.Source.CreateCommand("""
            UPDATE item_transaction_receipts SET result_records = jsonb_set(result_records, '{0,state,generationSeed}', '99', false)
            WHERE request_id = @id;
            """))
        { change.Parameters.AddWithValue("id", altered.Command.RequestId); Require(await change.ExecuteNonQueryAsync() == 1, "Private receipt fixture not altered."); }
        var mismatch = await altered.Commit();
        Require(mismatch.Status == DungeonPickupCommitStatus.IdempotencyConflict && mismatch.Receipt is null
            && (await altered.Items.FindAsync(altered.Command.Mutations[0].NewRecord.ItemId, default))!.State.GenerationSeed == 7,
            "Consumed retry accepted changed receipt body with the same fingerprint.");
        Console.WriteLine("PASS PostgresCommit: full receipt body mismatch cannot hide behind an unchanged fingerprint.");

        // Directly affected old caller: the optional authorization hook must not change reward commits.
        var reward = await Create();
        var rewards = new PostgresDungeonRewardCommitRepository(schema.Source, new PostgresEconomyRepository(schema.Source),
            (PostgresItemRepository)reward.Items, Microsoft.Extensions.Logging.Abstractions.NullLogger<PostgresDungeonRewardCommitRepository>.Instance);
        var rewardItem = reward.Command.Mutations[0].NewRecord;
        var grant = reward.Command with { Operation = "DungeonReward", Mutations = [new(0, rewardItem with
            { Location = new(ItemContainerType.Mail, "DungeonReward.Fixture", 0),
                Metadata = rewardItem.Metadata with { CreationSource = "DungeonReward" } })] };
        DungeonRewardCommitBatch batch = new([], [grant], [1]);
        Require((await rewards.CommitAsync(batch, reward.Now, default)).Succeeded
            && (await rewards.CommitAsync(batch, reward.Now, default)).Succeeded
            && (await reward.Items.FindByOwnerAsync(reward.Command.Actor, true, 100, default)).Count == 1
            && await schema.Scalar("SELECT count(*) FROM dungeon_pickup_intents WHERE request_id = @id", reward.Command.RequestId) == 0,
            "Existing reward caller/default transaction hook regressed.");
        Console.WriteLine("PASS PostgresCommit: existing reward transaction caller still grants and retries once without pickup consumption.");
    }

    private static async Task Wait(PrivateSchema schema, Task pending)
    {
        for (int i = 0; i < 100; i++)
        {
            if (await schema.WaitingLocks() > 0) { Require(!pending.IsCompleted, "Lock waiter completed before release."); return; }
            await Task.Delay(20);
        }
        throw new TestFailure("Actual fixture lock wait not observed.");
    }
}

sealed class MutableClock(DateTimeOffset now) : TimeProvider
{
    public DateTimeOffset Now { get; set; } = now;
    public override DateTimeOffset GetUtcNow() => Now;
}
