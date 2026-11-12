using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Data;

namespace ProjectRpg.Backend.Narrative;

public sealed record StorySkipRecord(string ChapterId, int CatalogVersion, DateTimeOffset AppliedAtUtc,
    ImmutableArray<string> QuestIds, string DefinitionHash);
public sealed record StorySkipQuest(string QuestId, string? Title);
public sealed record StorySkipPreview(Guid CharacterId, string ChapterId, string? Title, int CatalogVersion,
    string ExpectedRevision, ImmutableArray<StorySkipQuest> Quests, ImmutableArray<string> StoryUnlocks,
    int Cost, bool GrantsQuestRewards, bool RequiresOriginalTraining, string PreviewHash);
public sealed record StorySkipCommand(Guid RequestId, long ExpectedRevision, int CatalogVersion,
    string ChapterId, string PreviewHash, bool Confirmed)
{
    public void Validate()
    {
        if (RequestId == Guid.Empty || ExpectedRevision < 0 || CatalogVersion < 1 || !Confirmed
            || !NarrativeCommand.ValidId(ChapterId) || PreviewHash is null || PreviewHash.Length != 64
            || PreviewHash.Any(c => !(c is >= '0' and <= '9' or >= 'a' and <= 'f')))
            throw new NarrativeConflictException("invalid_story_skip_command");
    }
    public string Fingerprint()
    {
        Validate();
        return NarrativeStorySkip.Hash(JsonSerializer.Serialize(new { Kind = "story_skip_v1", ExpectedRevision,
            CatalogVersion, ChapterId, PreviewHash, Confirmed }));
    }
}

/// <summary>Approved policy v1: free, one authored chapter, no quest payout or combat/growth facts.</summary>
public sealed class NarrativeStorySkip(INarrativeStorySkipRepository repository, NarrativeCatalog catalog,
    NarrativeCoordinator narrative, TimeProvider clock)
{
    internal static string Hash(string text) => Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    private static string DefinitionHash(ChapterDefinition chapter, NarrativeCatalog catalog) => Hash(JsonSerializer.Serialize(new
    {
        Kind = "story_skip_definition_v1", chapter.Id,
        Quests = chapter.RequiredQuests.Order(StringComparer.Ordinal).Select(id => new
        { Id = id, catalog.Quests[id].Version, Prerequisites = catalog.Quests[id].Prerequisites.Order(StringComparer.Ordinal) }),
        Unlocks = chapter.StoryUnlocks.Order(StringComparer.Ordinal)
    }));

    internal static void ValidateRecords(NarrativeState state, NarrativeCatalog catalog)
    {
        if (state.StorySkips.IsDefault || state.StorySkips.Length > 256 || state.StorySkips.Any(s => s is null)
            || state.StorySkips.Select(s => s.ChapterId).Distinct(StringComparer.Ordinal).Count() != state.StorySkips.Length)
            throw new NarrativeConflictException("invalid_story_skip_state");
        HashSet<string> skipped = new(StringComparer.Ordinal);
        foreach (var skip in state.StorySkips)
        {
            if (!NarrativeCommand.ValidId(skip.ChapterId) || !catalog.Chapters.TryGetValue(skip.ChapterId, out var chapter) || !chapter.AllowStorySkip
                || skip.CatalogVersion < 1 || skip.CatalogVersion > catalog.Version
                || skip.DefinitionHash != DefinitionHash(chapter, catalog)
                || skip.AppliedAtUtc == default || skip.AppliedAtUtc.Offset != TimeSpan.Zero
                || skip.QuestIds.IsDefaultOrEmpty || !skip.QuestIds.SequenceEqual(skip.QuestIds.Order(StringComparer.Ordinal))
                || skip.QuestIds.Any(id => !chapter.RequiredQuests.Contains(id) || !skipped.Add(id)
                    || state.Quests.Any(q => q.QuestId == id && q.Status == QuestStatus.Completed))
                || !chapter.RequiredQuests.All(id => skip.QuestIds.Contains(id)
                    || state.Quests.Any(q => q.QuestId == id && q.Status == QuestStatus.Completed)))
                throw new NarrativeConflictException("invalid_story_skip_state");
        }
        foreach (var skip in state.StorySkips)
            foreach (string id in skip.QuestIds)
                if (!catalog.Quests[id].Prerequisites.All(state.SatisfiesStoryQuest))
                    throw new NarrativeConflictException("invalid_story_skip_state");
    }

    public async Task<StorySkipPreview> PreviewAsync(Guid characterId, string steamId, string chapterId, CancellationToken ct)
        => Build(characterId, chapterId, await repository.LoadStorySkipContextAsync(characterId, steamId, ct));

    private StorySkipPreview Build(Guid characterId, string chapterId, NarrativeStorySkipContext context)
    {
        if (context.AccountStates.IsDefaultOrEmpty || context.AccountStates.Any(s => s is null)
            || context.AccountStates.Count(s => s.CharacterId == characterId) != 1
            || context.AccountStates.Select(s => s.CharacterId).Distinct().Count() != context.AccountStates.Length)
            throw new NarrativeConflictException("invalid_narrative_state");
        foreach (var source in context.AccountStates) NarrativeCoordinator.ValidateState(source, catalog);
        if (!NarrativeCommand.ValidId(chapterId) || !catalog.Chapters.TryGetValue(chapterId, out var chapter)
            || !chapter.AllowStorySkip) throw new NarrativeConflictException("story_skip_not_authored");
        var state = context.AccountStates.Single(s => s.CharacterId == characterId);
        if (context.InDungeon) throw new NarrativeConflictException("story_skip_dungeon_busy");
        if (state.CompletedChapters.Contains(chapterId)) throw new NarrativeConflictException("story_skip_already_finished");
        // Only actual quest reports authorize another character's skip. Skips never extend account eligibility.
        if (!context.AccountStates.Any(s => chapter.RequiredQuests.All(id =>
            s.Quests.Any(q => q.QuestId == id && q.Status == QuestStatus.Completed))))
            throw new NarrativeConflictException("story_skip_not_eligible");
        var members = chapter.RequiredQuests.ToHashSet(StringComparer.Ordinal);
        if (members.Any(id => catalog.Quests[id].Prerequisites.Any(parent => !members.Contains(parent)
            && !state.SatisfiesStoryQuest(parent)))) throw new NarrativeConflictException("story_skip_prerequisite_missing");
        var quests = chapter.RequiredQuests.Where(id => !state.Quests.Any(q => q.QuestId == id && q.Status == QuestStatus.Completed))
            .Order(StringComparer.Ordinal).Select(id => new StorySkipQuest(id, catalog.Quests[id].Title)).ToImmutableArray();
        var unlocks = chapter.StoryUnlocks.Except(state.StoryUnlocks, StringComparer.Ordinal).Order(StringComparer.Ordinal).ToImmutableArray();
        var preview = new StorySkipPreview(characterId, chapterId, chapter.Title, catalog.Version,
            state.Revision.ToString(System.Globalization.CultureInfo.InvariantCulture), quests, unlocks, 0, false, true, "");
        return preview with { PreviewHash = Hash(JsonSerializer.Serialize(new { Kind = "story_skip_preview_v1", Plan = preview })) };
    }

    public async Task<NarrativeView> ApplyAsync(Guid characterId, string steamId, StorySkipCommand command, CancellationToken ct)
    {
        command.Validate();
        DateTimeOffset now = clock.GetUtcNow();
        var result = await repository.CommitStorySkipAsync(characterId, steamId, command, context =>
        {
            var plan = Build(characterId, command.ChapterId, context);
            var state = context.AccountStates.Single(s => s.CharacterId == characterId);
            if (command.CatalogVersion != catalog.Version) throw new NarrativeConflictException("narrative_catalog_changed");
            if (command.ExpectedRevision != state.Revision) throw new NarrativeConflictException("narrative_revision_conflict");
            if (command.PreviewHash != plan.PreviewHash) throw new NarrativeConflictException("story_skip_preview_changed");
            var skip = new StorySkipRecord(command.ChapterId, catalog.Version, now, [.. plan.Quests.Select(q => q.QuestId)],
                DefinitionHash(catalog.Chapters[command.ChapterId], catalog));
            var updated = new ScenarioManager().Project(state with { Revision = checked(state.Revision + 1),
                StorySkips = state.StorySkips.Add(skip) }, catalog);
            NarrativeCoordinator.ValidateState(updated, catalog);
            return updated;
        }, now, ct);
        return narrative.View(result);
    }
}
