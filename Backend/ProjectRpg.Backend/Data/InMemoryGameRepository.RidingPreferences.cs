using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : IRidingPreferencesStore
{
    private readonly Dictionary<Guid, RidingPreferences> _ridingPreferences = [];
    public Task<RidingPreferencesResult> SaveRidingPreferencesAsync(string authenticatedSteamId, Guid characterId,
        RidingPreferencesCommand command, RidingCatalog catalog, CancellationToken cancellationToken)
    {
        command = RidingPreferencesRules.Normalize(command);
        var (gate, items) = RidingAssets();
        lock (_dungeonGate) lock (gate.SyncRoot) lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested(); var now = gate.UtcNow;
            var character = RequireRidingOwner(authenticatedSteamId, characterId, now);
            var actor = new RidingRegistrationActor(characterId, character.RosterId);
            string fingerprint = RidingPreferencesRules.Fingerprint(actor, command);
            if (ReadRidingCommand(character, command.RequestId, items, cancellationToken) is { } old)
            {
                if (old.Preferences is not { } receipt || receipt.Fingerprint != fingerprint) throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
                return Task.FromResult(new RidingPreferencesResult(true, receipt));
            }
            EnsureServiceOpen();
            if ((_characterLeases.TryGetValue(characterId, out var lease) && lease.ExpiresAt > now)
                || _dungeonSessions.Values.Any(s => s.Members.Any(m => m.CharacterId == characterId)
                    && s.State is DungeonSessionState.Waiting or DungeonSessionState.Loading or DungeonSessionState.InProgress or DungeonSessionState.SettlementPending)
                || _dungeonRewardSettlements.Values.Any(s => s.CharacterIds.Contains(characterId)
                    && s.State is DungeonRewardSettlementState.Pending or DungeonRewardSettlementState.Processing))
                throw new RidingStoreException(RidingStoreFailure.RequiresLobby);
            var previous = _ridingPreferences.GetValueOrDefault(characterId);
            var collection = _ridingCollections.GetValueOrDefault(character.RosterId) ?? new(character.RosterId, 0, []);
            var updated = RidingPreferencesRules.Plan(command, previous?.Revision ?? 0, catalog, collection);
            var result = new RidingPreferencesReceipt(command.RequestId, RidingPreferencesRules.Operation, fingerprint, characterId, character.RosterId, updated, now);
            string json = JsonSerializer.Serialize(result);
            try
            {
                _ridingPreferences[characterId] = updated; _ridingReceipts.Add(command.RequestId, json);
                cancellationToken.ThrowIfCancellationRequested();
                return Task.FromResult(new RidingPreferencesResult(false, result with { Preferences = CopyPreferences(updated) }));
            }
            catch
            {
                if (previous is null) _ridingPreferences.Remove(characterId); else _ridingPreferences[characterId] = previous;
                _ridingReceipts.Remove(command.RequestId); throw;
            }
        }
    }
    public Task<RidingOwnerView> GetRidingOwnerViewAsync(string authenticatedSteamId, Guid characterId, CancellationToken cancellationToken)
    {
        var (gate, _) = RidingAssets();
        lock (_dungeonGate) lock (gate.SyncRoot) lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested(); var character = RequireRidingOwner(authenticatedSteamId, characterId, gate.UtcNow);
            var c = _ridingCollections.GetValueOrDefault(character.RosterId);
            return Task.FromResult(new RidingOwnerView(new(character.RosterId, c?.Revision ?? 0, Array.AsReadOnly(c?.Mounts.ToArray() ?? [])),
                CopyPreferences(_ridingPreferences.GetValueOrDefault(characterId) ?? RidingPreferences.Empty)));
        }
    }
    public Task<RidingCommandReceipt?> GetRidingCommandReceiptAsync(string authenticatedSteamId, Guid characterId, Guid requestId, CancellationToken cancellationToken)
    {
        var (gate, items) = RidingAssets();
        lock (_dungeonGate) lock (gate.SyncRoot) lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested(); var character = RequireRidingOwner(authenticatedSteamId, characterId, gate.UtcNow);
            if (requestId == Guid.Empty) throw new RidingRuleException(RidingFailure.InvalidRequest);
            return Task.FromResult(ReadRidingCommand(character, requestId, items, cancellationToken));
        }
    }
    private RidingCommandReceipt? ReadRidingCommand(GameCharacter character, Guid request, InMemoryItemRepository items, CancellationToken ct)
    {
        if (!_ridingReceipts.TryGetValue(request, out var json)) return null;
        using var document = JsonDocument.Parse(json); string? op = document.RootElement.GetProperty("Operation").GetString();
        if (op == RidingRegistrationRules.Operation) return new(op, ReadRidingReceipt(character, request, items, ct), null);
        if (op != RidingPreferencesRules.Operation) throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        var receipt = JsonSerializer.Deserialize<RidingPreferencesReceipt>(json) ?? throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        if (receipt.CharacterId != character.CharacterId || receipt.RosterId != character.RosterId) throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
        if (receipt.RequestId != request || receipt.Preferences.Revision <= 0) throw new RidingStoreException(RidingStoreFailure.InconsistentReceipt);
        return new(op, null, receipt with { Preferences = CopyPreferences(receipt.Preferences) });
    }
    private static RidingPreferences CopyPreferences(RidingPreferences p) => p with { FavoriteMountIds = Array.AsReadOnly(p.FavoriteMountIds.ToArray()) };
}
