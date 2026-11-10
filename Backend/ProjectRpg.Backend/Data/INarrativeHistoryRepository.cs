using System.Collections.Immutable;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public interface INarrativeHistoryRepository
{
    // A single read snapshot, scoped through ownership of the selected character. No progress writes.
    Task<ImmutableArray<NarrativeState>> LoadAccountNarrativeHistoryAsync(
        Guid characterId, string steamId, CancellationToken cancellationToken);
}
