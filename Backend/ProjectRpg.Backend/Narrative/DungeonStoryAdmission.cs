using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Data;

namespace ProjectRpg.Backend.Narrative;

/// <summary>Read-only, character-specific story eligibility. Growth and membership retain their existing owners.</summary>
public sealed class DungeonStoryAdmission
{
    private readonly DungeonContentCatalog content;
    private readonly NarrativeCatalog narrative;

    public DungeonStoryAdmission(DungeonContentCatalog content, NarrativeCatalog narrative)
    {
        var defined = narrative.Chapters.Values.SelectMany(c => c.StoryUnlocks).ToHashSet(StringComparer.Ordinal);
        if (!content.AllowedSelections.All(s => content.GetRequiredStoryUnlocks(s.DungeonId).All(defined.Contains)))
            throw new InvalidOperationException("DungeonContent: RequiredStoryUnlocks references an undefined narrative unlock.");
        this.content = content;
        this.narrative = narrative;
    }

    public async Task<string?> GetErrorAsync(IGameRepository repository, Guid characterId, string steamId,
        string dungeonId, CancellationToken cancellationToken)
    {
        var required = content.GetRequiredStoryUnlocks(dungeonId);
        if (required.IsEmpty) return null;
        try
        {
            NarrativeState state = await ((INarrativeRepository)repository).LoadNarrativeAsync(characterId, steamId, cancellationToken);
            NarrativeCoordinator.ValidateState(state, narrative);
            return required.All(state.StoryUnlocks.Contains) ? null : "story_prerequisite_not_met";
        }
        catch (NarrativeConflictException)
        {
            // Incompatible/contradictory saves cannot authorize entry. Never repair,
            // initialize or grant progress as a side effect of checking admission.
            return "story_progress_unavailable";
        }
    }

    public async Task<string?> GetMemberErrorAsync(IGameRepository repository, Guid sessionId, Guid characterId,
        string steamId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        var session = await repository.GetDungeonSessionAsync(sessionId, now, cancellationToken);
        // Leave missing sessions and foreign members to the existing endpoint's
        // ownership, membership, generation and reconnect validation.
        if (session is null || !session.Members.Any(m => m.CharacterId == characterId && m.SteamId == steamId))
            return null;
        return await GetErrorAsync(repository, characterId, steamId, session.DungeonId, cancellationToken);
    }
}
