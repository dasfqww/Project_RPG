using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : IRidingAdmissionSnapshotStore
{
    public Task<RidingAdmissionSnapshot?> GetRidingAdmissionSnapshotAsync(string serverId, Guid dungeonSessionId,
        Guid characterId, long connectionGeneration, CancellationToken cancellationToken)
    {
        var (gate, _) = RidingAssets();
        lock (_dungeonGate) lock (gate.SyncRoot) lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested(); var now = gate.UtcNow;
            if (string.IsNullOrWhiteSpace(serverId) || dungeonSessionId == Guid.Empty || characterId == Guid.Empty || connectionGeneration <= 0
                || !_characters.TryGetValue(characterId, out var character) || !_rosters.TryGetValue(character.RosterId, out var roster)
                || roster.SteamId != character.SteamId || !AccountAllowed(character.SteamId, now)
                || !IsAuthorizedGameServerSessionMember(dungeonSessionId, serverId, character.SteamId, characterId, now))
                return Task.FromResult<RidingAdmissionSnapshot?>(null);
            var session = _dungeonSessions[dungeonSessionId]; var member = session.Members.Single(m => m.CharacterId == characterId);
            if (member.ConnectionGeneration != connectionGeneration || member.ConfirmedConnectionGeneration != connectionGeneration
                || member.DisconnectedAt is not null || member.DisconnectedGeneration != 0
                || _dungeonAbandonments.ContainsKey((dungeonSessionId, characterId)) || _dungeonDisconnectionExpulsions.ContainsKey((dungeonSessionId, characterId)))
                return Task.FromResult<RidingAdmissionSnapshot?>(null);
            var collection = _ridingCollections.GetValueOrDefault(character.RosterId);
            var owner = new RidingOwnerView(new(character.RosterId, collection?.Revision ?? 0, Array.AsReadOnly(collection?.Mounts.ToArray() ?? [])),
                CopyPreferences(_ridingPreferences.GetValueOrDefault(characterId) ?? RidingPreferences.Empty));
            var deadline = new[] { session.ExpiresAt, member.LeaseExpiresAt, _characterLeases[characterId].ExpiresAt }.Min();
            return Task.FromResult<RidingAdmissionSnapshot?>(new(dungeonSessionId, characterId, character.RosterId, connectionGeneration, now, deadline, owner));
        }
    }
}
