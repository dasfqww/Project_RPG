using System.Text.Json;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

Guid session = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
Guid character = Guid.Parse("11111111-2222-3333-4444-555555555555");
Guid other = Guid.Parse("99999999-2222-3333-4444-555555555555");
const string version = "reward_v1";
// Golden vectors captured from the original worker's hash/byte-layout algorithm.
Require(DungeonRewardIdentity.CurrencyRequestId(session, character, version)
    == Guid.Parse("a854e6eb-38c6-809d-3c41-95ec1005d373"), "Existing currency receipt identity changed.");
Require(DungeonRewardIdentity.ItemRequestId(session, character, version)
    == Guid.Parse("c0fa6f6b-b483-4970-6b6b-add7d2732e05"), "Existing item receipt identity changed.");
Require(DungeonRewardIdentity.ItemInstance(session, character, version, 0)
    == (Guid.Parse("61228c83-e1c2-4979-ffd0-5b94d1a2c1a3"), 968868604), "Existing first item GUID/seed changed.");
Require(DungeonRewardIdentity.ItemInstance(session, character, version, 3)
    == (Guid.Parse("7222024d-057a-712d-deba-2f4204a11a95"), 854530066), "Sparse original reward index lost.");
Console.WriteLine("PASS identity: pre-refactor currency/item receipts, instance GUIDs and generation seeds");

List<string> tags = ["Item.Reward.Planning"];
List<ItemStatValue> stats = [new("Stat.Attack", 12)];
DungeonItemReward guaranteed = new("RPGItemDefinition", "Guaranteed", 1, 4,
    ItemBindState.CharacterBound, 10, 20, tags, stats, 999);
string seed = new('A', 64);
DungeonItemReward[] rewards = [guaranteed,
    guaranteed with { DefinitionName = "RareOne", Quantity = 1, MaxStackSize = 11, PersonalLootWeight = 1, PersonalLootSeed = seed },
    guaranteed with { DefinitionName = "RareTwo", Quantity = 1, MaxStackSize = 22, PersonalLootWeight = 1, PersonalLootSeed = seed },
    guaranteed with { DefinitionName = "Selected", Quantity = 2, MaxStackSize = 33, PersonalLootWeight = 9998, PersonalLootSeed = seed }];
DateTimeOffset now = DateTimeOffset.Parse("2026-11-01T00:00:00Z");
DungeonRewardSettlement settlement = new(session, "server", version, "frozen-command",
    DungeonRewardSettlementState.Processing, [other, character], [new("Gold", 15)], rewards,
    1, now, "worker", now.AddMinutes(1), null, now, now, null);
Dictionary<Guid, CharacterEconomyContext> contexts = new()
{
    [character] = new(character, Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"), "76561198000000001"),
    [other] = new(other, Guid.Parse("bbbbbbbb-bbbb-cccc-dddd-eeeeeeeeeeee"), "76561198000000002")
};
ContextReader reader = new(contexts);
DungeonRewardCommitBatch batch = await DungeonRewardCommitPlanner.BuildAsync(settlement, reader, default);
Require(reader.Reads.SequenceEqual(new[] { character, other }), "Member lookup order changed.");
Require(batch.CurrencyEntries.Count == 2 && batch.ItemRequests.Count == 2, "Party commands missing.");
Require(batch.CurrencyEntries[0].Context == contexts[character]
    && batch.CurrencyEntries[0].Request.RequestId == Guid.Parse("a854e6eb-38c6-809d-3c41-95ec1005d373")
    && batch.CurrencyEntries[0].Request.Changes.SequenceEqual(settlement.Changes), "Currency command/context changed.");
ItemRepositoryCommitRequest items = batch.ItemRequests[0];
Require(items.RequestId == Guid.Parse("c0fa6f6b-b483-4970-6b6b-add7d2732e05")
    && items.CommandFingerprint == $"frozen-command:{character:D}:items"
    && items.Actor == new ItemOwnerRef(ItemOwnerType.Character, character.ToString("D"))
    && items.AffectedQuantity == 6, "Selected quantity/owner/fingerprint changed.");
Require(items.Mutations.Select(x => x.NewRecord.Location.SlotIndex).SequenceEqual(new[] { 0, 3 })
    && batch.StackSizesFor(items)!.SequenceEqual(new[] { 999, 33 }), "Selection must preserve original indices and selected stack limits.");
ItemRecord selected = items.Mutations[1].NewRecord;
Require(selected.State.InstanceId == Guid.Parse("7222024d-057a-712d-deba-2f4204a11a95")
    && selected.State.GenerationSeed == 854530066
    && selected.Location.ContainerType == ItemContainerType.Mail
    && selected.Metadata.CreationSource == "DungeonClear.reward_v1", "Candidate metadata/identity changed.");
string serialized = JsonSerializer.Serialize(batch);
DungeonRewardCommitBatch retry = await DungeonRewardCommitPlanner.BuildAsync(settlement with
    { AttemptCount = 17, WorkerId = "replacement-worker", UpdatedAt = now.AddDays(1) }, new ContextReader(contexts), default);
Require(JsonSerializer.Serialize(retry) == serialized, "Retry/replacement worker changed frozen commands.");
tags.Add("Item.LaterMutation"); stats.Clear();
Require(items.Mutations[0].NewRecord.State.InstanceTags.Count == 1
    && items.Mutations[0].NewRecord.State.StatValues.Count == 1, "Candidate aliases mutable reward arrays.");
Console.WriteLine("PASS planning: input-only repository port, sorted party, sparse selection, immutable payload and identical replay");

DungeonRewardCommitBatch legacy = await DungeonRewardCommitPlanner.BuildAsync(settlement with
    { CharacterIds = [character], Changes = [], ItemRewards = [guaranteed with { MaxStackSize = 0 }] }, new ContextReader(contexts), default);
Require(legacy.CurrencyEntries.Count == 0 && legacy.StackSizesFor(legacy.ItemRequests.Single())!.Single() == 0
    && legacy.ItemRequests[0].Mutations[0].NewRecord.State.EquipmentProgression is null,
    "Legacy stack/growth values must not be inferred.");
DungeonRewardCommitBatch empty = await DungeonRewardCommitPlanner.BuildAsync(settlement with
    { Changes = [], ItemRewards = [] }, new ContextReader(contexts), default);
Require(empty.CurrencyEntries.Count == 0 && empty.ItemRequests.Count == 0, "Empty reward fabricated a command.");
Console.WriteLine("PASS compatibility: legacy zero stack limit and absent growth, item-only and empty rewards");

ContextReader missing = new(new Dictionary<Guid, CharacterEconomyContext>());
await Reject<DungeonRewardPlanningException>(() => DungeonRewardCommitPlanner.BuildAsync(settlement, missing, default));
Require(missing.Reads.SequenceEqual(new[] { character }), "Missing first member should stop before later lookups.");
await Reject<DungeonRewardPlanningException>(() => DungeonRewardCommitPlanner.BuildAsync(settlement with
    { ItemRewards = [rewards[3] with { PersonalLootSeed = null }] }, new ContextReader(contexts), default));
await Reject<IOException>(() => DungeonRewardCommitPlanner.BuildAsync(settlement,
    new ContextReader(contexts) { Failure = new IOException("transient") }, default));
using CancellationTokenSource canceled = new(); canceled.Cancel();
await Reject<OperationCanceledException>(() => DungeonRewardCommitPlanner.BuildAsync(settlement, new ContextReader(contexts), canceled.Token));
Console.WriteLine("PASS errors: missing member/invalid frozen loot permanent, transport transient, cancellation propagated");

static void Require(bool value, string error) { if (!value) throw new InvalidOperationException(error); }
static async Task Reject<T>(Func<Task<DungeonRewardCommitBatch>> action) where T : Exception
{
    try { await action(); }
    catch (T) { return; }
    throw new InvalidOperationException($"Expected {typeof(T).Name}.");
}

sealed class ContextReader(IReadOnlyDictionary<Guid, CharacterEconomyContext> contexts) : ICharacterEconomyRepository
{
    public List<Guid> Reads { get; } = [];
    public Exception? Failure { get; init; }
    public Task<CharacterEconomyContext?> GetCharacterEconomyContextAsync(Guid id, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        Reads.Add(id);
        if (Failure is not null) throw Failure;
        return Task.FromResult(contexts.GetValueOrDefault(id));
    }
}
