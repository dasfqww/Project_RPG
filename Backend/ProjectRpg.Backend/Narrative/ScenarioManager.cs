namespace ProjectRpg.Backend.Narrative;

/// <summary>Projects story completion/unlocks. Never creates combat milestones or grants.</summary>
public sealed class ScenarioManager
{
    public NarrativeState Project(NarrativeState state, NarrativeCatalog catalog)
    {
        ChapterDefinition[] completed = catalog.Chapters.Values
            .Where(c => c.RequiredQuests.All(id => state.Quests.Any(q => q.QuestId == id && q.Status == QuestStatus.Completed))
                || !state.StorySkips.IsDefault && state.StorySkips.Any(s => s.ChapterId == c.Id))
            .OrderBy(c => c.Id, StringComparer.Ordinal).ToArray();
        return state with
        {
            CompletedChapters = [.. completed.Select(c => c.Id)],
            StoryUnlocks = [.. completed.SelectMany(c => c.StoryUnlocks).Distinct(StringComparer.Ordinal)
                .Order(StringComparer.Ordinal)]
        };
    }
}
