using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : IRidingRegistrationStore
{
    private readonly Dictionary<Guid, RidingCollection> _ridingCollections = [];
    // Serialized parent receipts never share mutable item arrays with the common item repository or callers.
    private readonly Dictionary<Guid, string> _ridingReceipts = [];

    public Task<RidingRegistrationResult> RegisterMountAsync(string authenticatedSteamId, Guid characterId,
        RidingRegistrationCommand command, RidingCatalog catalog, CancellationToken cancellationToken)
    {
        var (gate, items) = RidingAssets();
        cancellationToken.ThrowIfCancellationRequested();
        // Same dungeon -> item -> character order as pickup/equipment/operations. No awaits under Monitor.
        // Dungeon gate covers account access, leases and settlements; item gate covers roster writes and reservations.
        lock (_dungeonGate) lock (gate.SyncRoot) lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            DateTimeOffset now = gate.UtcNow;
            GameCharacter character = RequireRidingOwner(authenticatedSteamId, characterId, now);
            var actor = new RidingRegistrationActor(character.CharacterId, character.RosterId);
            string fingerprint = RidingRegistrationRules.Fingerprint(actor, command);
            if (ReadRidingReceipt(character, command.RequestId, items, cancellationToken) is { } stored)
            {
                if (stored.Fingerprint != fingerprint) throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
                return Task.FromResult(new RidingRegistrationResult(true, stored));
            }
            EnsureServiceOpen();
            if ((_characterLeases.TryGetValue(characterId, out var lease) && lease.ExpiresAt > now)
                || _dungeonSessions.Values.Any(s => s.Members.Any(m => m.CharacterId == characterId)
                    && s.State is DungeonSessionState.Waiting or DungeonSessionState.Loading or DungeonSessionState.InProgress or DungeonSessionState.SettlementPending)
                || _dungeonRewardSettlements.Values.Any(s => s.CharacterIds.Contains(characterId)
                    && s.State is DungeonRewardSettlementState.Pending or DungeonRewardSettlementState.Processing))
                throw new RidingStoreException(RidingStoreFailure.RequiresLobby);
            RidingCollection? previous = _ridingCollections.GetValueOrDefault(character.RosterId);
            RidingCollection collection = previous ?? new(character.RosterId, 0, Array.Empty<RidingUnlock>());
            ItemRecord item = items.FindAsync(command.ItemId, cancellationToken).GetAwaiter().GetResult()
                ?? throw new RidingRuleException(RidingFailure.ItemUnavailable);
            var plan = RidingRegistrationRules.Plan(actor, command, catalog, item, collection.Revision,
                collection.Mounts.Select(m => m.MountId).ToHashSet(StringComparer.Ordinal), gate.IsItemReserved(item.ItemId), now);
            if (plan.ChildItemRequestId is { } child && items.TryGetCommitResultAsync(child, cancellationToken).GetAwaiter().GetResult() is not null)
                throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);

            // A whole-item snapshot is safe here: the SAME gate excludes every other item writer until commit/rollback.
            var itemSnapshot = plan.Consumption is null ? null : items.CaptureSnapshot();
            try
            {
                ItemRepositoryCommitResult? itemReceipt = null;
                if (plan.Consumption is { } consumption)
                {
                    var request = new ItemRepositoryCommitRequest(plan.ChildItemRequestId!.Value,
                        RidingRegistrationRules.Operation, plan.Fingerprint, item.Owner, 1, [consumption]);
                    itemReceipt = items.CommitAsync(request, now, cancellationToken).GetAwaiter().GetResult();
                    if (itemReceipt.Status != ItemRepositoryCommitStatus.Committed)
                        throw new RidingStoreException(RidingStoreFailure.ItemCommitFailed);
                }
                var receipt = new RidingRegistrationReceipt(command.RequestId, RidingRegistrationRules.Operation,
                    fingerprint, characterId, character.RosterId, plan.Outcome, plan.MountId, plan.NewRosterRevision, now, itemReceipt);
                string payload = JsonSerializer.Serialize(receipt);
                if (plan.Unlock is { } unlock)
                    _ridingCollections[character.RosterId] = new(character.RosterId, plan.NewRosterRevision,
                        Array.AsReadOnly(collection.Mounts.Append(unlock).OrderBy(m => m.MountId, StringComparer.Ordinal).ToArray()));
                _ridingReceipts.Add(command.RequestId, payload);
                cancellationToken.ThrowIfCancellationRequested();
                return Task.FromResult(new RidingRegistrationResult(false, CopyRidingReceipt(payload)));
            }
            catch
            {
                if (itemSnapshot is not null) items.RestoreSnapshot(itemSnapshot);
                if (previous is null) _ridingCollections.Remove(character.RosterId);
                else _ridingCollections[character.RosterId] = previous;
                _ridingReceipts.Remove(command.RequestId);
                throw;
            }
        }
    }

    public Task<RidingCollection> GetRidingCollectionAsync(string authenticatedSteamId, Guid characterId, CancellationToken cancellationToken)
    {
        var (gate, _) = RidingAssets();
        lock (_dungeonGate) lock (gate.SyncRoot) lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var character = RequireRidingOwner(authenticatedSteamId, characterId, gate.UtcNow);
            var collection = _ridingCollections.GetValueOrDefault(character.RosterId);
            return Task.FromResult(new RidingCollection(character.RosterId, collection?.Revision ?? 0,
                Array.AsReadOnly(collection?.Mounts.ToArray() ?? [])));
        }
    }

    public Task<RidingRegistrationReceipt?> GetRidingRegistrationReceiptAsync(string authenticatedSteamId, Guid characterId,
        Guid requestId, CancellationToken cancellationToken)
    {
        var (gate, items) = RidingAssets();
        lock (_dungeonGate) lock (gate.SyncRoot) lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var character = RequireRidingOwner(authenticatedSteamId, characterId, gate.UtcNow);
            if (requestId == Guid.Empty) throw new RidingRuleException(RidingFailure.InvalidRequest);
            return Task.FromResult(ReadRidingReceipt(character, requestId, items, cancellationToken));
        }
    }

    private (InMemoryTransactionGate Gate, InMemoryItemRepository Items) RidingAssets() =>
        (_starterTransactionGate ?? throw new InvalidOperationException("Riding requires the shared transaction gate."),
         _starterItems ?? throw new InvalidOperationException("Riding requires the shared item repository."));

    private GameCharacter RequireRidingOwner(string account, Guid characterId, DateTimeOffset now)
    {
        if (string.IsNullOrWhiteSpace(account)) throw new RidingStoreException(RidingStoreFailure.CharacterNotOwned);
        EnsureAccountAllowed(account, now);
        if (!_accounts.ContainsKey(account)
            || !_characters.TryGetValue(characterId, out var character) || character.SteamId != account
            || !_rosters.TryGetValue(character.RosterId, out var roster) || roster.SteamId != account)
            throw new RidingStoreException(RidingStoreFailure.CharacterNotOwned);
        return character;
    }

    private RidingRegistrationReceipt? ReadRidingReceipt(GameCharacter character, Guid requestId,
        InMemoryItemRepository items, CancellationToken cancellationToken)
    {
        if (!_ridingReceipts.TryGetValue(requestId, out string? payload)) return null;
        using var document = JsonDocument.Parse(payload);
        if (document.RootElement.GetProperty("Operation").GetString() != RidingRegistrationRules.Operation)
            throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
        var receipt = CopyRidingReceipt(payload);
        if (receipt.Operation != RidingRegistrationRules.Operation || receipt.CharacterId != character.CharacterId
            || receipt.RosterId != character.RosterId)
            throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
        if (receipt.Outcome == RidingRegistrationOutcome.Registered)
        {
            Guid childId = RidingRegistrationRules.ChildItemRequestId(requestId);
            var child = items.TryGetCommitResultAsync(childId, cancellationToken).GetAwaiter().GetResult();
            if (child is null || receipt.ItemReceipt is null || receipt.ItemReceipt.RequestId != childId
                || JsonSerializer.Serialize(child) != JsonSerializer.Serialize(receipt.ItemReceipt))
                throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        }
        else if (receipt.Outcome != RidingRegistrationOutcome.AlreadyOwned || receipt.ItemReceipt is not null)
            throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        return receipt;
    }

    private static RidingRegistrationReceipt CopyRidingReceipt(string payload) =>
        JsonSerializer.Deserialize<RidingRegistrationReceipt>(payload) ?? throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
}
