using System.Net;
using System.Net.Http.Json;
using System.Text;
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
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Checks;

static class ItemCommitReplayChecks
{
    private static readonly DateTimeOffset Now = new(2026, 10, 29, 0, 0, 0, TimeSpan.Zero);

    public static async Task Run(Func<IItemRepository> create)
    {
        await ChangedBodies(create);
        await HistoricalMultiItemReceipt(create);
        await LookupRace(create);
        await ExactInt64Revision();
        await Http(create);
        Console.WriteLine("PASS item replay integrity: 5 focused groups; Memory/service and real loopback HTTP only.");
    }

    public static async Task RunPostgres(PrivateSchema schema)
    {
        await ChangedBodies(schema.Items, "Postgres");
        await HistoricalMultiItemReceipt(schema.Items, "Postgres");
        await LookupRace(schema.Items, "Postgres");
        await FreshRepositoryAndRows(schema);
        await Http(schema.Items, "Postgres", schema.Repository);
        Console.WriteLine("PASS PostgreSQL item replay integrity: 5 focused groups; no Memory fallback or UE/BP verification.");
    }

    public static async Task RunIngress(Func<IItemRepository> create)
    {
        await Http(create, verifyIngress: true);
        Console.WriteLine("PASS item commit ingress: focused HTTP/Memory checks; no PostgreSQL or UE/BP verification.");
    }

    public static async Task RunRevision(Func<IItemRepository> create)
    {
        await RevisionLimits(create);
        await ExactInt64Revision();
        await Http(create, verifyRevision: true);
        Console.WriteLine("PASS item revision limits: 3 focused groups; no PostgreSQL or UE/BP verification.");
    }

    private static ItemRepositoryCommitRequest Command()
    {
        var owner = new ItemOwnerRef(ItemOwnerType.Character, Guid.NewGuid().ToString("D"));
        var record = new ItemRecord("RPGItemDefinition", "Potion", 1, owner,
            new(ItemContainerType.Inventory, owner.OwnerId, 0),
            new(Guid.NewGuid(), 7, 7, ["Item.A", "Item.B"], [new("Stat.A", 1), new("Stat.B", 2)]),
            0, ItemLifecycleState.Active,
            new(ItemBindState.Unbound, new(5, 10), Now.AddDays(1), "WorldPickup", false));
        return new(Guid.NewGuid(), "Item.Pickup", "same-declared-fingerprint", owner, 7, [new(0, record)]);
    }

    private static ItemTransactionService Service(IItemRepository repository) => new(repository, new FixedClock(Now));
    private static string Snapshot(ItemRepositoryCommitResult receipt) => JsonSerializer.Serialize(receipt);

    private static async Task ChangedBodies(Func<IItemRepository> create, string label = "Memory")
    {
        IItemRepository items = create();
        var command = Command();
        var service = Service(items);
        var first = await service.CommitAsync(command, default);
        Require(first.Status == ItemRepositoryCommitStatus.Committed, "First commit failed.");
        string original = Snapshot(first);
        ItemRecord record = command.Mutations[0].NewRecord;
        var changedRecords = new ItemRecord[]
        {
            record with { DefinitionType = "OtherDefinition" },
            record with { DefinitionName = "OtherPotion" },
            record with { DefinitionVersion = 2 },
            record with { Owner = new(ItemOwnerType.Character, Guid.NewGuid().ToString("D")) },
            record with { Location = record.Location with { ContainerType = ItemContainerType.Mail } },
            record with { Location = record.Location with { ContainerId = "another-container" } },
            record with { Location = record.Location with { SlotIndex = 1 } },
            record with { State = record.State with { InstanceId = Guid.NewGuid() } },
            record with { State = record.State with { GenerationSeed = 8 } },
            record with { State = record.State with { Quantity = 8 } },
            record with { State = record.State with { InstanceTags = ["Item.C", "Item.B"] } },
            record with { State = record.State with { InstanceTags = ["Item.B", "Item.A"] } },
            record with { State = record.State with { StatValues = [new("Stat.A", 2), new("Stat.B", 2)] } },
            record with { State = record.State with { StatValues = [new("Stat.C", 1), new("Stat.B", 2)] } },
            record with { State = record.State with { StatValues = [new("Stat.B", 2), new("Stat.A", 1)] } },
            record with { Metadata = record.Metadata with { BindState = ItemBindState.CharacterBound } },
            record with { Metadata = record.Metadata with { Durability = new(4, 10) } },
            record with { Metadata = record.Metadata with { Durability = new(5, 11) } },
            record with { Metadata = record.Metadata with { ExpiresAtUtc = Now.AddDays(2) } },
            record with { Metadata = record.Metadata with { CreationSource = "OtherSource" } },
            record with { Metadata = record.Metadata with { IsLocked = true } },
            record with { LifecycleState = ItemLifecycleState.Consumed, Location = new(ItemContainerType.Terminal, "", -1),
                State = record.State with { Quantity = 0 } }
        };
        var variants = changedRecords.Select(r => command with { Mutations = [new(0, r)] }).ToList();
        variants.Add(command with { AffectedQuantity = 8 });
        variants.Add(command with { Operation = "Item.Move" });
        variants.Add(command with { CommandFingerprint = "another-fingerprint" });
        variants.Add(command with { Actor = new(ItemOwnerType.Character, Guid.NewGuid().ToString("D")) });
        variants.Add(command with { Mutations = [new(1, record with { Revision = 1 })] });
        variants.Add(command with { Mutations = [command.Mutations[0], new(0, record with {
            State = record.State with { InstanceId = Guid.NewGuid() }, Location = record.Location with { SlotIndex = 1 } })] });
        foreach (var variant in variants)
        {
            Require(ItemRecordRules.TryValidateCommit(variant, out _), "Replay case was invalid before receipt comparison.");
            var result = await service.CommitAsync(variant, default);
            Require(result.Status == ItemRepositoryCommitStatus.IdempotencyConflict && result.Records.Count == 0,
                "Changed command was acknowledged or exposed the original records.");
            Require(Snapshot((await items.TryGetCommitResultAsync(command.RequestId, default))!) == original,
                "Changed command rewrote the receipt.");
        }
        var reloaded = command with { Mutations = [new(0, ItemRecordRules.SnapshotWithRevision(record, 0))] };
        var retry = await service.CommitAsync(reloaded, default);
        Require(retry.Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && Snapshot(retry with { Status = first.Status }) == original, "Deep-copied exact retry failed.");
        Require((await items.FindByOwnerAsync(command.Actor, true, 500, default)).Count == 1
            && (await items.FindAsync(record.ItemId, default)) is { Revision: 1, State.Quantity: 7 }, "Replay changed items.");
        Console.WriteLine($"PASS {label}: {variants.Count} changed bodies rejected; exact retry and original records/receipt preserved.");
    }

    private static async Task HistoricalMultiItemReceipt(Func<IItemRepository> create, string label = "Memory")
    {
        IItemRepository items = create();
        var service = Service(items);
        var seed = Command();
        var left = seed.Mutations[0].NewRecord;
        var right = left with { State = left.State with { InstanceId = Guid.NewGuid() }, Location = left.Location with { SlotIndex = 1 } };
        seed = seed with { Mutations = [new(0, left), new(0, right)] };
        Require((await service.CommitAsync(seed, default)).Status == ItemRepositoryCommitStatus.Committed, "Multi seed failed.");
        left = left with { Revision = 1, Location = new(ItemContainerType.Terminal, "", -1),
            LifecycleState = ItemLifecycleState.Consumed, State = left.State with { Quantity = 0 } };
        right = right with { Revision = 1, Location = right.Location with { SlotIndex = 0 } };
        var change = seed with { RequestId = Guid.NewGuid(), Operation = "Item.Merge", Mutations = [new(1, left), new(1, right)] };
        var receipt = await service.CommitAsync(change, default);
        Require(receipt.Status == ItemRepositoryCommitStatus.Committed && receipt.Records[0].LifecycleState == ItemLifecycleState.Consumed,
            "Multi-item terminal commit failed.");
        var moved = right with { Revision = 2, Location = right.Location with { SlotIndex = 2 } };
        var next = change with { RequestId = Guid.NewGuid(), Operation = "Item.Move", Mutations = [new(2, moved)] };
        Require((await service.CommitAsync(next, default)).Status == ItemRepositoryCommitStatus.Committed, "Later move failed.");
        var retry = await service.CommitAsync(change, default);
        Require(retry.Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && Snapshot(retry with { Status = receipt.Status }) == Snapshot(receipt), "Historical receipt was compared to current state.");
        var missing = await service.CommitAsync(change with { Mutations = [change.Mutations[0]] }, default);
        var reordered = await service.CommitAsync(change with { Mutations = change.Mutations.Reverse().ToArray() }, default);
        Require(missing.Status == ItemRepositoryCommitStatus.IdempotencyConflict
            && reordered.Status == ItemRepositoryCommitStatus.IdempotencyConflict, "Changed multi-item body accepted.");
        Require((await items.FindAsync(right.ItemId, default)) is { Revision: 3, Location.SlotIndex: 2 }, "Old retry undid a later move.");
        Console.WriteLine($"PASS {label}: multi-item/tombstone and later item changes preserve the exact historical receipt.");
    }

    private static async Task LookupRace(Func<IItemRepository> create, string label = "Memory")
    {
        var items = create();
        var command = Command();
        Require((await Service(items).CommitAsync(command, default)).Status == ItemRepositoryCommitStatus.Committed, "Race seed failed.");
        var race = new ReceiptRepository(items);
        var result = await Service(race).CommitAsync(command with { AffectedQuantity = 8 }, default);
        Require(race.Commits == 1 && result.Status == ItemRepositoryCommitStatus.IdempotencyConflict && result.Records.Count == 0,
            "Receipt created between lookup and commit bypassed the comparison.");
        var exactRace = new ReceiptRepository(items);
        Require((await Service(exactRace).CommitAsync(command, default)).Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && exactRace.Commits == 1, "Exact racing retry failed.");
        Console.WriteLine($"PASS {label}: both mismatched and exact retries checked after a simulated lookup/commit race.");
    }

    private static async Task FreshRepositoryAndRows(PrivateSchema schema)
    {
        var command = Command();
        var first = await Service(schema.Items()).CommitAsync(command, default);
        Require(first.Status == ItemRepositoryCommitStatus.Committed, "Postgres persistence seed failed.");

        // Compare all SQL columns, not just the subset projected back to gameplay.
        // The existing fixture's search_path confines these reads to its private schema.
        async Task<string> Rows()
        {
            await using var query = schema.Source.CreateCommand("""
                SELECT (SELECT row_to_json(i)::text FROM item_records i WHERE item_id = @item),
                       (SELECT row_to_json(r)::text FROM item_transaction_receipts r WHERE request_id = @request),
                       (SELECT count(*) FROM item_records),
                       (SELECT count(*) FROM item_transaction_receipts);
                """);
            query.Parameters.AddWithValue("item", command.Mutations[0].NewRecord.ItemId);
            query.Parameters.AddWithValue("request", command.RequestId);
            await using var reader = await query.ExecuteReaderAsync();
            Require(await reader.ReadAsync() && !reader.IsDBNull(0) && !reader.IsDBNull(1), "Persisted rows missing.");
            return JsonSerializer.Serialize(new { Item = reader.GetString(0), Receipt = reader.GetString(1),
                Items = reader.GetInt64(2), Receipts = reader.GetInt64(3) });
        }

        string originalRows = await Rows();
        var fresh = Service(schema.Items());
        var record = command.Mutations[0].NewRecord;
        var exact = command with { Mutations = [new(0, ItemRecordRules.SnapshotWithRevision(record, 0))] };
        var retry = await fresh.CommitAsync(exact, default);
        Require(retry.Status == ItemRepositoryCommitStatus.AlreadyCommitted
            && Snapshot(retry with { Status = first.Status }) == Snapshot(first), "Fresh repository lost the original receipt.");
        var changed = await fresh.CommitAsync(command with { Mutations = [new(0, record with {
            State = record.State with { Quantity = record.State.Quantity + 1 } })] }, default);
        Require(changed.Status == ItemRepositoryCommitStatus.IdempotencyConflict && changed.Records.Count == 0,
            "Fresh repository acknowledged a changed quantity.");
        Require(await Rows() == originalRows, "Replay changed full persisted rows or item/receipt counts.");
        Console.WriteLine("PASS Postgres: fresh repository replays the original JSONB receipt; full SQL rows/counts unchanged.");
    }

    private static async Task RevisionLimits(Func<IItemRepository> create)
    {
        var command = Command();
        var record = command.Mutations[0].NewRecord;
        var lastRecord = record with { Revision = long.MaxValue };
        Require(ItemRecordRules.TryValidateRecord(lastRecord, out _), "Last stored revision became unreadable.");
        var lastWritable = command with { Mutations = [new(long.MaxValue - 1, record with { Revision = long.MaxValue - 1 })] };
        Require(ItemRecordRules.TryValidateCommit(lastWritable, out _), "Last writable revision rejected.");

        IItemRepository items = create();
        var first = await Service(items).CommitAsync(command, default);
        Require(first.Status == ItemRepositoryCommitStatus.Committed, "Revision seed failed.");
        string original = Snapshot(first);
        foreach (long revision in new[] { -1L, long.MaxValue })
        {
            var rejected = command with { RequestId = Guid.NewGuid(), Mutations = [
                new(0, record with { State = record.State with { InstanceId = Guid.NewGuid() },
                    Location = record.Location with { SlotIndex = 1 } }),
                new(revision, record with { Revision = revision })] };
            Require(!ItemRecordRules.TryValidateCommit(rejected, out _), "Invalid revision passed domain validation.");
            var untouched = new ReceiptRepository(null, first);
            var serviceResult = await Service(untouched).CommitAsync(rejected, default);
            Require(serviceResult.Status == ItemRepositoryCommitStatus.InvalidRequest && serviceResult.Records.Count == 0
                && untouched.Lookups == 0 && untouched.Commits == 0, "Invalid revision reached the repository.");
            var direct = await items.CommitAsync(rejected, Now, default);
            Require(direct.Status == ItemRepositoryCommitStatus.InvalidRequest && direct.Records.Count == 0
                && await items.TryGetCommitResultAsync(rejected.RequestId, default) is null,
                "Invalid revision wrote a receipt or partially committed.");
        }
        Require((await items.FindAsync(record.ItemId, default)) is { Revision: 1, State.Quantity: 7 }
            && (await items.FindByOwnerAsync(command.Actor, true, 500, default)).Count == 1
            && Snapshot((await items.TryGetCommitResultAsync(command.RequestId, default))!) == original,
            "Rejected revision changed the original item/receipt or inserted a sibling mutation.");
        Console.WriteLine("PASS revision domain/service/Memory: negative/max rejected before repository access; mixed writes unchanged; max stored record readable.");
    }

    private static async Task ExactInt64Revision()
    {
        var command = Command();
        const long revision = 9007199254740993L;
        command = command with { Mutations = [new(revision, command.Mutations[0].NewRecord with { Revision = revision })] };
        var receipt = new ItemRepositoryCommitResult(ItemRepositoryCommitStatus.Committed, command.RequestId, command.Operation,
            command.CommandFingerprint, command.Actor, command.AffectedQuantity,
            [ItemRecordRules.SnapshotWithRevision(command.Mutations[0].NewRecord, revision + 1)], Now);
        var items = new ReceiptRepository(null, receipt);
        Require((await Service(items).CommitAsync(command, default)).Status == ItemRepositoryCommitStatus.AlreadyCommitted,
            "Int64 revision lost precision.");
        var adjacent = command with { Mutations = [new(revision - 1, command.Mutations[0].NewRecord with { Revision = revision - 1 })] };
        Require((await Service(items).CommitAsync(adjacent, default)).Status == ItemRepositoryCommitStatus.IdempotencyConflict,
            "Adjacent Int64 revisions compared equal.");
        var overflow = command with { Mutations = [new(long.MaxValue, command.Mutations[0].NewRecord with { Revision = long.MaxValue })] };
        int lookupsBeforeOverflow = items.Lookups;
        Require((await Service(items).CommitAsync(overflow, default)).Status == ItemRepositoryCommitStatus.InvalidRequest
            && items.Lookups == lookupsBeforeOverflow && items.Commits == 0,
            "Revision overflow acknowledged.");
        var lastWritable = command with { Mutations = [new(long.MaxValue - 1,
            command.Mutations[0].NewRecord with { Revision = long.MaxValue - 1 })] };
        var lastReceipt = receipt with { Records = [ItemRecordRules.SnapshotWithRevision(lastWritable.Mutations[0].NewRecord, long.MaxValue)] };
        Require((await Service(new ReceiptRepository(null, lastReceipt)).CommitAsync(lastWritable, default)).Status
            == ItemRepositoryCommitStatus.AlreadyCommitted, "Exact last writable revision retry failed.");
        Console.WriteLine("PASS service: exact Int64 revisions above 2^53 and overflow rejection (synthetic stored receipt).");
    }

    private static async Task Http(Func<IItemRepository> create, string label = "Memory",
        Func<IGameRepository>? createGame = null, bool verifyIngress = false, bool verifyRevision = false)
    {
        var fixture = await Fixture.Create(createGame?.Invoke() ?? new InMemoryGameRepository(), items: create());
        TimeProvider clock = new FixedClock(fixture.Now);
        var tokens = new AccessTokenService(clock);
        var server = tokens.Issue(TimeSpan.FromHours(1));
        var player = tokens.Issue(TimeSpan.FromHours(1));
        await fixture.Repo.StoreGameServerCredentialAsync(server.TokenHash, fixture.Server, fixture.Session, server.ExpiresAt, default);
        await fixture.Repo.StoreSessionAsync(player.TokenHash, fixture.A.SteamId, player.ExpiresAt, default);
        var builder = WebApplication.CreateBuilder();
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(fixture.Repo); builder.Services.AddSingleton(fixture.Items);
        builder.Services.AddSingleton(clock); builder.Services.AddSingleton(tokens);
        builder.Services.AddGameRepositoryContracts();
        builder.Services.AddSingleton<ItemTransactionService>(); builder.Services.AddSingleton<ItemOwnerAccessService>();
        builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
        builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions()));
        builder.Services.AddScoped<BearerAuthenticator>();
        await using var app = builder.Build();
        app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapItemApi();
        await app.StartAsync();
        using var client = new HttpClient { BaseAddress = new(app.Urls.Single()), Timeout = TimeSpan.FromSeconds(10) };
        async Task<HttpResponseMessage> Send(ItemRepositoryCommitRequest command, string? token)
        {
            using var request = new HttpRequestMessage(HttpMethod.Post, "/api/item-transactions/commit");
            if (token is not null) request.Headers.Authorization = new("Bearer", token);
            request.Content = JsonContent.Create(DungeonPickupIntentRules.ToContract(command));
            return await client.SendAsync(request);
        }
        foreach (var (token, status) in new[] { ((string?)null, 401), ("invalid", 401), (player.Token, 403) })
        {
            using var denied = await Send(fixture.Command, token);
            Require((int)denied.StatusCode == status, "Generic item HTTP authentication changed.");
        }
        using var first = await Send(fixture.Command, server.Token);
        Require(first.StatusCode == HttpStatusCode.OK, "Generic item HTTP first commit failed.");
        using var retry = await Send(fixture.Command, server.Token);
        Require(retry.StatusCode == HttpStatusCode.OK && (await retry.Content.ReadAsStringAsync()).Contains("AlreadyCommitted"),
            "Generic item HTTP exact retry failed.");
        var record = fixture.Command.Mutations[0].NewRecord;
        using var changed = await Send(fixture.Command with { Mutations = [new(0, record with {
            Location = record.Location with { SlotIndex = 1 } })] }, server.Token);
        using var body = JsonDocument.Parse(await changed.Content.ReadAsStringAsync());
        Require(changed.StatusCode == HttpStatusCode.Conflict
            && body.RootElement.GetProperty("status").GetString() == "IdempotencyConflict"
            && body.RootElement.GetProperty("records").GetArrayLength() == 0, "HTTP acknowledged another destination or exposed old records.");
        Require((await fixture.Items.FindAsync(record.ItemId, default)) is { Revision: 1, Location.SlotIndex: 0 }, "HTTP replay changed item.");
        if (verifyRevision)
        {
            string original = Snapshot((await fixture.Items.TryGetCommitResultAsync(fixture.Command.RequestId, default))!);
            foreach (long revision in new[] { -1L, long.MaxValue })
            {
                var invalid = fixture.Command with { RequestId = Guid.NewGuid(), Mutations = [
                    new(0, record with { State = record.State with { InstanceId = Guid.NewGuid() },
                        Location = record.Location with { SlotIndex = 1 } }),
                    new(revision, record with { Revision = revision })] };
                using var rejected = await Send(invalid, server.Token);
                Require(rejected.StatusCode == HttpStatusCode.BadRequest
                    && await fixture.Items.TryGetCommitResultAsync(invalid.RequestId, default) is null,
                    "HTTP invalid revision did not reject before committing.");
            }
            var writable = fixture.Command with { RequestId = Guid.NewGuid(), Mutations = [
                new(long.MaxValue - 1, record with { Revision = long.MaxValue - 1 })] };
            using var mismatch = await Send(writable, server.Token);
            using var mismatchBody = JsonDocument.Parse(await mismatch.Content.ReadAsStringAsync());
            Require(mismatch.StatusCode == HttpStatusCode.Conflict
                && mismatchBody.RootElement.GetProperty("status").GetString() == "RevisionConflict",
                "Last writable revision lost precision or bypassed CAS.");
            Require((await fixture.Items.FindAsync(record.ItemId, default)) is { Revision: 1, Location.SlotIndex: 0 }
                && (await fixture.Items.FindByOwnerAsync(fixture.Command.Actor, true, 500, default)).Count == 1
                && Snapshot((await fixture.Items.TryGetCommitResultAsync(fixture.Command.RequestId, default))!) == original,
                "HTTP revision rejection changed the item/receipt or inserted a sibling mutation.");
            Console.WriteLine("PASS HTTP/Memory revision: negative/max return 400 without writes; max-1 preserves exact CAS/409.");
        }
        if (verifyIngress)
        {
            string originalReceipt = Snapshot((await fixture.Items.TryGetCommitResultAsync(fixture.Command.RequestId, default))!);
            int cases = 0;
            async Task ExpectRaw(string name, string? token, string payload, string mediaType, int expected,
                bool chunked = false, Encoding? encoding = null)
            {
                using var request = new HttpRequestMessage(HttpMethod.Post, "/api/item-transactions/commit");
                if (token is not null) request.Headers.Authorization = new("Bearer", token);
                request.Content = new StringContent(payload, encoding ?? Encoding.UTF8, mediaType);
                if (chunked) request.Headers.TransferEncodingChunked = true;
                using var response = await client.SendAsync(request);
                Require((int)response.StatusCode == expected, "Item ingress failed: " + name);
                cases++;
            }

            const int limit = ItemApiEndpoints.MaximumCommitBodyBytes;
            string oversized = new(' ', limit + 1);
            await ExpectRaw("missing bearer before JSON parse", null, "{", "application/json", 401);
            await ExpectRaw("invalid bearer before JSON parse", "invalid", "{", "application/json", 401);
            await ExpectRaw("player before JSON parse", player.Token, "{", "application/json", 403);
            await ExpectRaw("player before media type validation", player.Token, "{", "text/plain", 403);
            await ExpectRaw("player before length validation", player.Token, oversized, "application/json", 403);
            await ExpectRaw("server non-JSON", server.Token, "{}", "text/plain", 415);
            await ExpectRaw("server malformed JSON", server.Token, "{", "application/json", 400);
            await ExpectRaw("server null JSON", server.Token, "null", "application/json", 400);
            await ExpectRaw("server empty JSON", server.Token, "", "application/json", 400);
            await ExpectRaw("server oversized Content-Length", server.Token, oversized, "application/json", 413);
            await ExpectRaw("server oversized chunked", server.Token, oversized, "application/json", 413, chunked: true);
            await ExpectRaw("encoded bytes, not characters", server.Token, new string('가', limit / 3 + 1), "application/json", 413);
            var tooMany = fixture.Command with { Mutations = Enumerable.Repeat(fixture.Command.Mutations[0],
                ItemRecordRules.MaximumMutationCount + 1).ToArray() };
            await ExpectRaw("excess mutations", server.Token,
                JsonSerializer.Serialize(DungeonPickupIntentRules.ToContract(tooMany)), "application/json", 400);
            string valid = JsonSerializer.Serialize(DungeonPickupIntentRules.ToContract(fixture.Command));
            await ExpectRaw("exact byte limit", server.Token,
                valid + new string(' ', limit - Encoding.UTF8.GetByteCount(valid)), "application/json", 200);
            await ExpectRaw("JSON media type suffix", server.Token, valid, "application/vnd.project-rpg+json", 200);
            await ExpectRaw("existing UTF-16 support", server.Token, valid, "application/json", 200, encoding: Encoding.Unicode);

            Require(Snapshot((await fixture.Items.TryGetCommitResultAsync(fixture.Command.RequestId, default))!) == originalReceipt
                && (await fixture.Items.FindByOwnerAsync(fixture.Command.Actor, true, 500, default)).Count == 1
                && (await fixture.Items.FindAsync(record.ItemId, default)) is { Revision: 1, Location.SlotIndex: 0 },
                "Ingress validation or exact retry changed the original item/receipt.");
            Console.WriteLine($"PASS HTTP/Memory ingress: {cases} authorization/size/JSON/compatibility cases; original item/receipt preserved.");
        }
        await app.StopAsync();
        Console.WriteLine($"PASS HTTP/{label}: real auth and commit endpoint preserve exact replay; changed destination returns 409 without records.");
    }

    // Hides the preflight receipt to exercise the service's post-commit race check,
    // or supplies a synthetic historical receipt for exact Int64 comparisons.
    private sealed class ReceiptRepository(IItemRepository? inner, ItemRepositoryCommitResult? receipt = null) : IItemRepository
    {
        public int Commits { get; private set; }
        public int Lookups { get; private set; }
        public Task<ItemRepositoryCommitResult?> TryGetCommitResultAsync(Guid requestId, CancellationToken cancellationToken)
        {
            Lookups++;
            return Task.FromResult(receipt);
        }
        public Task<ItemRepositoryCommitResult> CommitAsync(ItemRepositoryCommitRequest request, DateTimeOffset now, CancellationToken cancellationToken)
        {
            Commits++;
            return inner!.CommitAsync(request, now, cancellationToken);
        }
        public Task<ItemRecord?> FindAsync(Guid itemId, CancellationToken cancellationToken) => throw new NotSupportedException();
        public Task<IReadOnlyList<ItemRecord>> FindByOwnerAsync(ItemOwnerRef owner, bool includeTerminal, int limit,
            CancellationToken cancellationToken) => throw new NotSupportedException();
    }
}
