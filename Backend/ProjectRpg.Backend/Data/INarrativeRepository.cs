using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public interface INarrativeRepository
{
    Task<NarrativeState> LoadNarrativeAsync(Guid characterId, string steamId, CancellationToken cancellationToken);
    // Ownership, receipt replay, revision check, transition, state and receipt commit share one lock/transaction.
    Task<NarrativeState> CommitNarrativeAsync(Guid characterId, string steamId, NarrativeCommand command,
        Func<NarrativeState, NarrativeState> transition, DateTimeOffset now, CancellationToken cancellationToken);
}
