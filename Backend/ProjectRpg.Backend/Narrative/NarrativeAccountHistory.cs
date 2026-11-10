using System.Collections.Immutable;
using ProjectRpg.Backend.Data;

namespace ProjectRpg.Backend.Narrative;

public sealed record AccountChapterHistory(string ChapterId,
    bool CompletedOnSelectedCharacter, bool CompletedOnOtherCharacter, string? Title = null);
public sealed record NarrativeAccountHistoryView(Guid CharacterId, int CatalogVersion,
    ImmutableArray<AccountChapterHistory> Chapters);

/// <summary>Private completion history, not a skip command, entitlement or cross-character unlock.</summary>
public sealed class NarrativeAccountHistory(INarrativeHistoryRepository repository, NarrativeCatalog catalog)
{
    public async Task<NarrativeAccountHistoryView> LoadAsync(Guid characterId, string steamId, CancellationToken ct)
    {
        var states = await repository.LoadAccountNarrativeHistoryAsync(characterId, steamId, ct);
        if (states.IsDefaultOrEmpty || states.Count(s => s.CharacterId == characterId) != 1
            || states.Select(s => s.CharacterId).Distinct().Count() != states.Length)
            throw new NarrativeConflictException("invalid_narrative_state");
        var selected = new HashSet<string>(StringComparer.Ordinal);
        var others = new HashSet<string>(StringComparer.Ordinal);
        foreach (var state in states)
        {
            ct.ThrowIfCancellationRequested();
            NarrativeCoordinator.ValidateState(state, catalog);
            // Count direct quest reports, never a bare chapter/unlock flag or an unreported goal.
            var reported = state.Quests.Where(q => q.Status == QuestStatus.Completed)
                .Select(q => q.QuestId).ToHashSet(StringComparer.Ordinal);
            var destination = state.CharacterId == characterId ? selected : others;
            foreach (var chapter in catalog.Chapters.Values)
                if (chapter.RequiredQuests.All(reported.Contains)) destination.Add(chapter.Id);
        }
        return new(characterId, catalog.Version,
            [.. selected.Union(others).Order(StringComparer.Ordinal)
                .Select(id => new AccountChapterHistory(id, selected.Contains(id), others.Contains(id), catalog.Chapters[id].Title))]);
    }
}
