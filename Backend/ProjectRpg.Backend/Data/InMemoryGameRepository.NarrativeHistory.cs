using System.Collections.Immutable;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : INarrativeHistoryRepository
{
    public Task<ImmutableArray<NarrativeState>> LoadAccountNarrativeHistoryAsync(
        Guid characterId, string steamId, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_characterGate)
        {
            cancellationToken.ThrowIfCancellationRequested();
            RequireNarrativeOwner(characterId, steamId);
            return Task.FromResult(_characters.Values.Where(c => c.SteamId == steamId)
                .OrderBy(c => c.CharacterId)
                .Select(c => _narrative.GetValueOrDefault(c.CharacterId) ?? NarrativeState.Create(c.CharacterId))
                .ToImmutableArray());
        }
    }
}
