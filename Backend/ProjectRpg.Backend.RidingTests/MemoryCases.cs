using System.Reflection;
using System.Text.Json;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Riding;
using ProjectRpg.Operations.Core.Restrictions;
using ProjectRpg.Operations.Core.ServiceControl;

static partial class Program
{
    sealed class RidingClock : TimeProvider
    {
        public DateTimeOffset Now = Program.Now;
        public Action? OnRead;
        public override DateTimeOffset GetUtcNow() { OnRead?.Invoke(); return Now; }
    }
    sealed class MemoryFixture
    {
        public const string Account = "76561198000000001";
        public readonly RidingClock Clock = new();
        public readonly InMemoryTransactionGate Gate;
        public readonly InMemoryItemRepository Items;
        public readonly InMemoryGameRepository Game;
        public readonly InMemoryTradeReservationRepository Reservations;
        public readonly GameCharacter Character;
        public MemoryFixture()
        {
            Gate = new(Clock); Items = new(Gate); Game = new(Gate, Items, Clock);
            Wait(Game.UpsertAccountAsync(new(Account, Account, false, false), Now, default));
            Character = NewCharacter();
            var economy = new InMemoryEconomyRepository(Gate);
            Wait(economy.UpsertDefinitionAsync(new("Gold", "Gold", CurrencyScope.Character, 1000000, true), Now, default));
            Reservations = new(Gate, Items, economy, Game,
                new TradeItemPolicy([new(Mapping.Item.Type, Mapping.Item.Name, Mapping.Item.Version, true, true, 99)]));
        }
        public GameCharacter NewCharacter() => Wait(Game.CreateCharacterAsync(Account, Guid.NewGuid().ToString("N"), Now, default));
        public ItemRecord Seed(GameCharacter? character = null, int quantity = 2, int slot = 80, string? name = null)
        {
            character ??= Character;
            string owner = character.CharacterId.ToString("D");
            var item = Item(quantity) with { Owner = new(ItemOwnerType.Character, owner), Location = new(ItemContainerType.Inventory, owner, slot),
                Revision = 0, DefinitionName = name ?? Mapping.Item.Name,
                State = Item(quantity).State with { InstanceId = Guid.NewGuid() },
                Metadata = Item(quantity).Metadata with { BindState = ItemBindState.Unbound } };
            var result = Wait(Items.CommitAsync(new(Guid.NewGuid(), "Test.Seed", "seed", item.Owner, quantity, [new(0, item)]), Now, default));
            Check(result.Status == ItemRepositoryCommitStatus.Committed, "Seed must use real Item V2 commit");
            return result.Records.Single();
        }
        public RidingRegistrationCommand CommandFor(ItemRecord item) => new(Guid.NewGuid(), item.ItemId, item.Revision, 7);
        public RidingRegistrationResult Register(RidingRegistrationCommand command, GameCharacter? character = null, RidingCatalog? catalog = null, CancellationToken ct = default) =>
            Wait(Game.RegisterMountAsync(Account, (character ?? Character).CharacterId, command, catalog ?? Catalog(), ct));
        public RidingCollection Collection(GameCharacter? character = null) => Wait(Game.GetRidingCollectionAsync(Account, (character ?? Character).CharacterId, default));
        public RidingRegistrationReceipt? Receipt(Guid id, GameCharacter? character = null) => Wait(Game.GetRidingRegistrationReceiptAsync(Account, (character ?? Character).CharacterId, id, default));
        public ItemRecord Current(ItemRecord item) => Wait(Items.FindAsync(item.ItemId, default))!;
        public TradeReservationCommand Hold(ItemRecord? item = null) => new(Guid.NewGuid(), Guid.NewGuid(), 1,
            new(Account, Character.CharacterId), "Gold", item is null ? TradeOffer.Empty : new([new(item.ItemId, item.Revision, 1)], 0), Clock.Now.AddMinutes(1));
    }
    static T Wait<T>(Task<T> task) => task.GetAwaiter().GetResult();
    static void Wait(Task task) => task.GetAwaiter().GetResult();
    static T Private<T>(object target, string field) => (T)(target.GetType().GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(target)
        ?? throw new InvalidOperationException("Missing fixture field " + field));
    static void StoreReject(RidingStoreFailure code, Action action)
    { assertions++; try { action(); } catch (RidingStoreException e) when (e.Code == code) { return; } throw new InvalidOperationException("Expected store failure " + code); }
    static void Throws<T>(Action action) where T : Exception
    { assertions++; try { action(); } catch (T) { return; } throw new InvalidOperationException("Expected " + typeof(T).Name); }
    static T[] Concurrent<T>(params Func<T>[] actions)
    {
        using var start = new ManualResetEventSlim();
        var tasks = actions.Select(action => Task.Run(() => { start.Wait(); return action(); })).ToArray();
        start.Set(); return Wait(Task.WhenAll(tasks));
    }
    static void MemoryCases()
    {
        Case("memory_atomic_commit", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item); var result = f.Register(command);
            var receipt = result.Receipt; var stored = f.Current(item); var collection = f.Collection();
            Check(!result.Replayed && receipt.Outcome == RidingRegistrationOutcome.Registered, "First registration commits");
            Check(stored.State.Quantity == 1 && stored.Revision == 2, "Exactly one consumed and common revision incremented");
            Check(collection.Revision == 1 && collection.Mounts.Single().MountId == "horse", "Shared collection published once");
            Check(collection.Mounts.Single().SourceRequestId == command.RequestId && receipt.CommittedAtUtc == Now, "Server clock/provenance retained");
            var child = Wait(f.Items.TryGetCommitResultAsync(RidingRegistrationRules.ChildItemRequestId(command.RequestId), default));
            Check(child is { Status: ItemRepositoryCommitStatus.Committed, AffectedQuantity: 1 } && child.CommandFingerprint == receipt.Fingerprint
                && child.Operation == RidingRegistrationRules.Operation, "Real common child receipt committed");
            Check(JsonSerializer.Serialize(f.Receipt(command.RequestId)) == JsonSerializer.Serialize(receipt), "Queryable exact parent receipt");
            Check(f.Collection(f.NewCharacter()).Revision == 1, "Same roster alternate character shares unlock");
        });
        Case("memory_terminal_commit", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(quantity: 1); var command = f.CommandFor(item); f.Register(command);
            var stored = f.Current(item);
            Check(stored.State.Quantity == 0 && stored.Revision == 2 && stored.LifecycleState == ItemLifecycleState.Consumed
                && stored.Location == new ItemLocation(ItemContainerType.Terminal, "", -1), "Real last-item tombstone");
            Check(f.Register(command).Replayed, "Consumed original item does not block receipt replay");
        });
        Case("memory_replay_current_item_and_catalog_independent", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item); var original = f.Register(command);
            var stored = f.Current(item); var moved = stored with { Location = new(ItemContainerType.CharacterStorage, stored.Owner.OwnerId, 5) };
            Check(Wait(f.Items.CommitAsync(new(Guid.NewGuid(), "Item.Move", "move", stored.Owner, 0, [new(stored.Revision, moved)]), Now.AddSeconds(1), default)).Status
                == ItemRepositoryCommitStatus.Committed, "Actual subsequent inventory mutation");
            f.Clock.Now = Now.AddDays(2);
            var nextCatalog = new RidingCatalog(1, 8, new(), [Wolf], []);
            var replay = f.Register(command, catalog: nextCatalog);
            Check(replay.Replayed && JsonSerializer.Serialize(replay.Receipt) == JsonSerializer.Serialize(original.Receipt), "Replay before stale CAS/expiry/catalog checks");
            Check(f.Current(item).Revision == 3 && f.Current(item).State.Quantity == 1, "Replay leaves current inventory untouched");
        });
        Case("memory_same_request_concurrency", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item);
            var results = Concurrent(Enumerable.Range(0, 16).Select(_ => (Func<RidingRegistrationResult>)(() => f.Register(command))).ToArray());
            Check(results.Count(r => !r.Replayed) == 1 && results.Count(r => r.Replayed) == 15, "Concurrent retries have one commit");
            Check(results.Select(r => JsonSerializer.Serialize(r.Receipt)).Distinct().Count() == 1, "Identical immutable first outcomes");
            Check(f.Current(item).State.Quantity == 1 && f.Collection().Revision == 1, "Concurrent replay never consumes twice");
        });
        Case("memory_shared_roster_concurrency", () =>
        {
            var f = new MemoryFixture(); var second = f.NewCharacter(); var a = f.Seed(); var b = f.Seed(second);
            var ca = f.CommandFor(a); var cb = f.CommandFor(b);
            var results = Concurrent(() => f.Register(ca), () => f.Register(cb, second));
            Check(results.Count(r => r.Receipt.Outcome == RidingRegistrationOutcome.Registered) == 1
                && results.Count(r => r.Receipt.Outcome == RidingRegistrationOutcome.AlreadyOwned) == 1, "Same roster locks serialize new unlock");
            Check(f.Current(a).State.Quantity + f.Current(b).State.Quantity == 3 && f.Collection().Revision == 1, "Only winner consumes");
            var duplicate = results.Single(r => r.Receipt.Outcome == RidingRegistrationOutcome.AlreadyOwned).Receipt;
            Check(duplicate.ItemReceipt is null && duplicate.RosterRevision == 1, "AlreadyOwned persists parent without child/revision write");
            Check(f.Receipt(ca.RequestId) is not null && f.Receipt(cb.RequestId, second) is not null, "Both callers retain their own receipt");
            Check(f.Register(ca).Replayed && f.Register(cb, second).Replayed, "Both outcomes replay");
        });
        Case("memory_different_mount_concurrency", () =>
        {
            var f = new MemoryFixture(); var a = f.Seed(); var b = f.Seed(slot: 81, name: "GameItem.Consume.Mount.Wolf");
            var catalog = new RidingCatalog(1, 7, new(), [Horse, Wolf], [Mapping, new(new("RPGItemDefinition", b.DefinitionName, 4), "wolf", 99, RidingItemUse.RegisterMount)]);
            var results = Concurrent(() => f.Register(f.CommandFor(a), catalog: catalog), () => f.Register(f.CommandFor(b), catalog: catalog));
            Check(results.All(r => r.Receipt.Outcome == RidingRegistrationOutcome.Registered), "Independent unlocks both commit");
            Check(f.Collection().Revision == 2 && f.Collection().Mounts.Select(m => m.MountId).SequenceEqual(new[] { "horse", "wolf" }), "No lost roster update");
            Check(f.Current(a).Revision == 2 && f.Current(b).Revision == 2, "Both items use common revisions");
        });
        Case("memory_request_scope_conflicts", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item); f.Register(command);
            StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Register(command with { ExpectedItemRevision = 2 }));
            StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Register(command with { CatalogVersion = 8 }));
            StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Register(command with { ItemId = Guid.NewGuid() }));
            var other = f.NewCharacter();
            StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Register(command, other));
            StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Receipt(command.RequestId, other));
            StoreReject(RidingStoreFailure.CharacterNotOwned, () => Wait(f.Game.GetRidingCollectionAsync("76561198000000002", f.Character.CharacterId, default)));
            StoreReject(RidingStoreFailure.CharacterNotOwned, () => Wait(f.Game.RegisterMountAsync("76561198000000002", f.Character.CharacterId, command, Catalog(), default)));
            StoreReject(RidingStoreFailure.CharacterNotOwned, () => Wait(f.Game.GetRidingRegistrationReceiptAsync(MemoryFixture.Account, Guid.NewGuid(), command.RequestId, default)));
            Check(f.Current(item).State.Quantity == 1 && f.Collection().Revision == 1, "Conflicts have no effects");
        });
        Case("memory_current_account_and_service_access", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item); f.Register(command);
            // Stored operations-state fixtures exercise the same live account/service guards used by production paths.
            var id = Guid.NewGuid(); var restrictions = Private<Dictionary<string, RestrictionReceipt>>(f.Game, "_accountRestrictions");
            restrictions[MemoryFixture.Account] = new(id, "Test.Restriction", Now, "fixture", new("Account", MemoryFixture.Account), "fixture restriction",
                new(MemoryFixture.Account, "Active", 1, Now, Now.AddMinutes(1), 0), Guid.NewGuid(), null);
            Throws<AccountRestrictedException>(() => f.Register(command));
            Throws<AccountRestrictedException>(() => f.Receipt(command.RequestId));
            Throws<AccountRestrictedException>(() => f.Collection());
            restrictions.Clear();
            typeof(InMemoryGameRepository).GetField("_serviceControl", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(f.Game, ServiceSnapshot.Initial with { Mode = "Draining" });
            Check(f.Register(command).Replayed, "Service/lobby writes cannot invalidate existing success");
            Throws<ServiceDrainingException>(() => f.Register(f.CommandFor(f.Current(item))));
            Check(f.Current(item).State.Quantity == 1, "Closed service does not consume");
        });
        Case("memory_lobby_and_unsettled_guards", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item); f.Register(command);
            Wait(f.Game.CreateDungeonSessionAsync(MemoryFixture.Account, f.Character.CharacterId, "fixture", "Normal", Now, Now.AddMinutes(1), default));
            Check(f.Register(command).Replayed && f.Receipt(command.RequestId) is not null, "Dungeon admission still allows owned receipt replay");
            StoreReject(RidingStoreFailure.RequiresLobby, () => f.Register(f.CommandFor(f.Current(item))));
            f.Clock.Now = Now.AddMinutes(2);
            StoreReject(RidingStoreFailure.RequiresLobby, () => f.Register(f.CommandFor(f.Current(item))));
            foreach (var state in new[] { DungeonRewardSettlementState.Pending, DungeonRewardSettlementState.Processing })
            {
                var pending = new MemoryFixture(); var token = pending.Seed(); var c = pending.CommandFor(token); var session = Guid.NewGuid();
                Private<Dictionary<Guid, DungeonRewardSettlement>>(pending.Game, "_dungeonRewardSettlements")[session] =
                    new(session, "fixture", "fixture", "fixture", state, [pending.Character.CharacterId], [], [], 0, Now, null, null, null, Now, Now, null);
                StoreReject(RidingStoreFailure.RequiresLobby, () => pending.Register(c));
                Check(pending.Current(token).Revision == 1 && pending.Collection().Revision == 0 && pending.Receipt(c.RequestId) is null, "Unsettled rewards do not leak writes");
            }
        });
        Case("memory_live_reservations_and_expiry", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item); var hold = f.Hold(item);
            Wait(f.Reservations.ReserveAsync(hold, default));
            Reject(RidingFailure.AssetReserved, () => f.Register(command));
            Check(f.Current(item).Revision == 1 && f.Receipt(command.RequestId) is null && f.Collection().Revision == 0, "Reserved rejection is atomic");
            f.Clock.Now = hold.ExpiresAt;
            Check(f.Register(command).Receipt.Outcome == RidingRegistrationOutcome.Registered, "Expired hold ignored using authoritative gate clock");
            var expired = new MemoryFixture(); var token = expired.Seed(); expired.Clock.Now = token.Metadata.ExpiresAtUtc!.Value;
            Reject(RidingFailure.ItemExpired, () => expired.Register(expired.CommandFor(token)));
            Check(expired.Collection().Revision == 0 && expired.Current(token).Revision == 1, "Item expiry uses current store clock");
        });
        Case("memory_reservation_registration_race", () =>
        {
            for (int attempt = 0; attempt < 8; attempt++)
            {
                var f = new MemoryFixture(); var item = f.Seed(); var hold = f.Hold(item); var command = f.CommandFor(item);
                var outcomes = Concurrent(() => { try { f.Register(command); return "registered"; } catch (RidingRuleException e) when (e.Code == RidingFailure.AssetReserved) { return "reserved"; } },
                    () => { try { Wait(f.Reservations.ReserveAsync(hold, default)); return "held"; } catch (TradeRuleException e) when (e.Code == "item_revision_conflict") { return "stale"; } });
                Check(outcomes.SequenceEqual(new[] { "registered", "stale" }) || outcomes.SequenceEqual(new[] { "reserved", "held" }), "Only valid reservation/consumption ordering");
                bool registered = outcomes[0] == "registered";
                Check(f.Current(item).State.Quantity == (registered ? 1 : 2) && f.Collection().Revision == (registered ? 1 : 0), "Race state agrees with sole winner");
            }
        });
        Case("memory_child_receipt_collision", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var other = f.Seed(slot: 81); var command = f.CommandFor(item);
            var collision = new ItemRepositoryCommitRequest(RidingRegistrationRules.ChildItemRequestId(command.RequestId), "Test.Other", "other", other.Owner, 0, [new(other.Revision, other)]);
            Wait(f.Items.CommitAsync(collision, Now, default));
            StoreReject(RidingStoreFailure.IdempotencyConflict, () => f.Register(command));
            Check(f.Current(item).Revision == 1 && f.Collection().Revision == 0 && f.Receipt(command.RequestId) is null, "Orphan child cannot be adopted as successful consumption");
            Check(Wait(f.Items.TryGetCommitResultAsync(collision.RequestId, default))!.Operation == "Test.Other", "Foreign child receipt preserved");
        });
        Case("memory_cancel_after_child_commit_rolls_back", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item);
            Wait(f.Reservations.ReserveAsync(f.Hold(), default));
            using var cts = new CancellationTokenSource(); int reads = 0;
            // 1: transaction clock, 2: plan reservation scan, 3: common item commit reservation scan.
            // The third read cancels AFTER CommitCore's entry check; it still writes the real child before outer rollback.
            f.Clock.OnRead = () => { if (++reads == 3) cts.Cancel(); };
            Throws<OperationCanceledException>(() => f.Register(command, ct: cts.Token));
            f.Clock.OnRead = null;
            Check(reads == 3 && cts.IsCancellationRequested, "Deterministic cancellation inside common commit, no product test hook");
            Check(f.Current(item).Revision == 1 && f.Current(item).State.Quantity == 2 && f.Collection().Revision == 0, "Item and unlock/revision rolled back");
            Check(f.Receipt(command.RequestId) is null && Wait(f.Items.TryGetCommitResultAsync(RidingRegistrationRules.ChildItemRequestId(command.RequestId), default)) is null, "Both receipts rolled back");
            Check(f.Register(command).Receipt.Outcome == RidingRegistrationOutcome.Registered && f.Current(item).Revision == 2, "Same command can commit after rollback");
        });
        Case("memory_receipt_and_collection_isolation", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item); var result = f.Register(command);
            ((IList<ItemRecord>)result.Receipt.ItemReceipt!.Records)[0] = Item();
            var read = f.Receipt(command.RequestId)!;
            Check(read.ItemReceipt!.Records.Single().ItemId == item.ItemId, "Caller cannot corrupt retained parent or common child");
            ((IList<string>)read.ItemReceipt.Records.Single().State.InstanceTags)[0] = "Corrupted";
            Check(f.Register(command).Receipt.ItemReceipt!.Records.Single().State.InstanceTags[0] == "Item.Rarity.Rare", "Every replay is a fresh deep snapshot");
            Throws<NotSupportedException>(() => ((IList<RidingUnlock>)f.Collection().Mounts).Clear());
            var childId = RidingRegistrationRules.ChildItemRequestId(command.RequestId);
            Private<Dictionary<Guid, ItemRepositoryCommitResult>>(f.Items, "_receipts").Remove(childId);
            StoreReject(RidingStoreFailure.InconsistentReceipt, () => f.Register(command));
            StoreReject(RidingStoreFailure.InconsistentReceipt, () => f.Receipt(command.RequestId));
            Check(f.Current(item).State.Quantity == 1 && f.Collection().Revision == 1, "Corruption fails closed without retry consumption");
        });
        Case("memory_fail_closed_configuration_and_cancel", () =>
        {
            var f = new MemoryFixture(); var item = f.Seed(); var command = f.CommandFor(item);
            using var cts = new CancellationTokenSource(); cts.Cancel();
            Throws<OperationCanceledException>(() => f.Register(command, ct: cts.Token));
            Reject(RidingFailure.DefinitionMismatch, () => f.Register(command with { CatalogVersion = 8 }));
            Reject(RidingFailure.ItemUnavailable, () => f.Register(command with { ItemId = Guid.NewGuid() }));
            Throws<InvalidOperationException>(() => Wait(new InMemoryGameRepository().RegisterMountAsync(MemoryFixture.Account, f.Character.CharacterId, command, Catalog(), default)));
            Check(f.Collection().Revision == 0 && f.Receipt(command.RequestId) is null && f.Current(item).Revision == 1, "Rejected calls publish no state");
        });
    }
}
