using System.Collections.Immutable;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed record NarrativeStorySkipContext(ImmutableArray<NarrativeState> AccountStates, bool InDungeon);

public interface INarrativeStorySkipRepository
{
    Task<NarrativeStorySkipContext> LoadStorySkipContextAsync(Guid characterId, string steamId, CancellationToken ct);
    // Owner lock, receipt replay, current account completion read, session guard, transition and
    // state/receipt persistence share the transaction. A preview is never an authorization receipt.
    Task<NarrativeState> CommitStorySkipAsync(Guid characterId, string steamId, StorySkipCommand command,
        Func<NarrativeStorySkipContext, NarrativeState> transition, DateTimeOffset now, CancellationToken ct);
}
