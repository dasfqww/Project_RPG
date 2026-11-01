using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    private readonly Dictionary<Guid, DungeonPickupIntent> _pickupIntents = new();

    internal DungeonPickupCommitResult CommitPickup(InMemoryItemRepository items, Guid sessionId, string serverId,
        Guid requestId, TimeProvider clock, CancellationToken cancellation)
    {
        // Parent authority first, then the same item/economy transaction gate used by ordinary commits.
        lock (_dungeonGate)
        lock (items.TransactionSyncRoot)
        {
            cancellation.ThrowIfCancellationRequested();
            var now = clock.GetUtcNow(); // Sample AFTER both potentially blocking locks.
            if (!_dungeonSessions.TryGetValue(sessionId, out var session) || session.ServerId != serverId
                || session.State != DungeonSessionState.InProgress || session.ExpiresAt <= now)
                return new(DungeonPickupCommitStatus.Forbidden);
            if (!_pickupIntents.TryGetValue(requestId, out var intent) || intent.DungeonSessionId != sessionId)
                return new(DungeonPickupCommitStatus.NotFound);
            if (intent.ServerId != serverId || PickupMember(sessionId, serverId, intent.CharacterId, now) != intent.SteamId)
                return new(DungeonPickupCommitStatus.Forbidden);
            var command = DungeonPickupIntentRules.RestoreCommand(intent);
            var receipt = items.TryGetCommitResultAsync(requestId, cancellation).GetAwaiter().GetResult();
            if (intent.ConsumedAt is not null)
                return receipt is null ? new(DungeonPickupCommitStatus.InternalError)
                    : DungeonPickupReceiptRules.Matches(command, receipt)
                        ? DungeonPickupCommitResult.FromItem(receipt with { Status = ItemRepositoryCommitStatus.AlreadyCommitted })
                        : new(DungeonPickupCommitStatus.IdempotencyConflict);
            // A generic/legacy receipt, even with the same fingerprint/body, is not evidence of atomic consumption.
            if (receipt is not null) return new(DungeonPickupCommitStatus.IdempotencyConflict);
            var snapshot = items.CaptureSnapshot();
            try
            {
                var result = items.CommitAsync(command, now, cancellation).GetAwaiter().GetResult();
                if (result.Status != ItemRepositoryCommitStatus.Committed)
                {
                    items.RestoreSnapshot(snapshot);
                    return DungeonPickupCommitResult.FromItem(result);
                }
                if (!DungeonPickupReceiptRules.Matches(command, result))
                    throw new InvalidOperationException("Pickup receipt did not match its frozen origin.");
                _pickupIntents[requestId] = intent with { ConsumedAt = result.CommittedAt };
                return DungeonPickupCommitResult.FromItem(result);
            }
            catch
            {
                items.RestoreSnapshot(snapshot);
                _pickupIntents[requestId] = intent;
                throw;
            }
        }
    }

    private string? PickupMember(Guid sessionId, string serverId, Guid characterId, DateTimeOffset now)
    {
        if (!_dungeonSessions.TryGetValue(sessionId, out var session)
            || session.ServerId != serverId || session.State != DungeonSessionState.InProgress || session.ExpiresAt <= now
            || !_characters.TryGetValue(characterId, out var character)
            || _dungeonAbandonments.ContainsKey((sessionId, characterId))
            || !_characterLeases.TryGetValue(characterId, out var lease)
            || lease.DungeonSessionId != sessionId || lease.ExpiresAt <= now) return null;
        return session.Members.Any(m => m.CharacterId == characterId && m.SteamId == character.SteamId
            && m.LeaseExpiresAt > now) ? character.SteamId : null;
    }

    public Task<DungeonPickupIntentResult> PreparePickupAsync(Guid sessionId, string serverId, Guid dropId,
        ItemRepositoryCommitRequest command, DateTimeOffset now, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!DungeonPickupIntentRules.ValidScope(sessionId, serverId)
            || !DungeonPickupIntentRules.TryFreeze(dropId, command, out var json))
            return Task.FromResult(new DungeonPickupIntentResult(DungeonPickupIntentStatus.InvalidRequest));
        command = DungeonPickupIntentRules.RestoreCommand(json);
        lock (_dungeonGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Guid character = Guid.Parse(command.Actor.OwnerId);
            string? steam = PickupMember(sessionId, serverId, character, now);
            if (steam is null) return Task.FromResult(new DungeonPickupIntentResult(DungeonPickupIntentStatus.Forbidden));
            Guid item = command.Mutations[0].NewRecord.ItemId;
            var existing = _pickupIntents.Values.Where(i => i.RequestId == command.RequestId || i.ItemId == item
                || (i.DungeonSessionId == sessionId && i.DropId == dropId)).ToArray();
            if (existing.Length != 0)
                return Task.FromResult(existing.Length == 1 && existing[0].SteamId == steam
                    && DungeonPickupIntentRules.Same(existing[0], sessionId, serverId, dropId, json)
                    ? new DungeonPickupIntentResult(DungeonPickupIntentStatus.AlreadyPrepared, existing[0])
                    : new DungeonPickupIntentResult(DungeonPickupIntentStatus.Conflict));
            var intent = new DungeonPickupIntent(sessionId, serverId, dropId, command.RequestId, character,
                steam, item, json, now);
            _pickupIntents.Add(command.RequestId, intent);
            return Task.FromResult(new DungeonPickupIntentResult(DungeonPickupIntentStatus.Prepared, intent));
        }
    }

    public Task<DungeonPickupIntentResult> ReadPickupIntentAsync(Guid sessionId, string serverId, Guid requestId,
        DateTimeOffset now, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!DungeonPickupIntentRules.ValidScope(sessionId, serverId) || requestId == Guid.Empty)
            return Task.FromResult(new DungeonPickupIntentResult(DungeonPickupIntentStatus.InvalidRequest));
        lock (_dungeonGate)
        {
            if (!_dungeonSessions.TryGetValue(sessionId, out var session) || session.ServerId != serverId
                || session.State != DungeonSessionState.InProgress || session.ExpiresAt <= now)
                return Task.FromResult(new DungeonPickupIntentResult(DungeonPickupIntentStatus.Forbidden));
            if (!_pickupIntents.TryGetValue(requestId, out var intent) || intent.DungeonSessionId != sessionId)
                return Task.FromResult(new DungeonPickupIntentResult(DungeonPickupIntentStatus.NotFound));
            return Task.FromResult(intent.ServerId == serverId && PickupMember(sessionId, serverId, intent.CharacterId, now) == intent.SteamId
                ? new DungeonPickupIntentResult(DungeonPickupIntentStatus.Prepared, intent)
                : new DungeonPickupIntentResult(DungeonPickupIntentStatus.Forbidden));
        }
    }
}
