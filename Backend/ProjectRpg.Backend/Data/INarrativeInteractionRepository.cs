using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public interface INarrativeInteractionRepository
{
    // Session/member/lease validation, character serialization, transition and receipt are atomic.
    // No-effect events keep the revision and still receive a durable receipt.
    Task<NarrativeState> CommitNarrativeInteractionAsync(Guid sessionId, Guid characterId, string serverId,
        NarrativeInteractionCommand command, Func<NarrativeState, NarrativeInteractionContext, NarrativeState> transition,
        DateTimeOffset now, CancellationToken cancellationToken);
}
