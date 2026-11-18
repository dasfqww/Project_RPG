using ProjectRpg.Backend.Achievements;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository : IAchievementRepository
{
    private readonly object achievementGate = new();
    private readonly Dictionary<(Guid Roster, AchievementTrack Track), Dictionary<Guid, DateTimeOffset>> achievementFacts = new();

    private void RecordAchievement(Guid roster, AchievementTrack track, Guid source, DateTimeOffset time)
    {
        var key = (roster, track);
        if (!achievementFacts.TryGetValue(key, out var times)) achievementFacts.Add(key, times = new());
        if (!times.TryGetValue(source, out var existing) || time < existing) times[source] = time;
    }

    private void RecordDungeonAchievementFacts(DungeonSession session, DungeonRewardSettlement receipt)
    {
        var track = new AchievementTrack("DungeonClear", session.DungeonId, session.Difficulty);
        lock (achievementGate)
            foreach (var id in receipt.CharacterIds)
                if (_characters.TryGetValue(id, out var character))
                    RecordAchievement(character.RosterId, track, receipt.DungeonSessionId, receipt.CreatedAt);
    }

    private void RecordChapterAchievementFacts(NarrativeState state, DateTimeOffset time)
    {
        var roster = _characters[state.CharacterId].RosterId;
        lock (achievementGate)
            foreach (var chapter in state.CompletedChapters)
                if (!state.StorySkips.Any(s => s.ChapterId == chapter))
                    RecordAchievement(roster, new("ChapterCompleted", chapter, ""), roster, time);
    }

    public Task<AchievementSnapshot?> ReadAchievementsAsync(Guid characterId, string steamId, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        if (!_characters.TryGetValue(characterId, out var character) || character.SteamId != steamId)
            return Task.FromResult<AchievementSnapshot?>(null);
        lock (achievementGate)
        {
            var times = AchievementCatalog.Current.Thresholds.ToDictionary(t => t.Key, t =>
                achievementFacts.TryGetValue((character.RosterId, t.Key), out var facts)
                    ? facts.Values.Order().Take(t.Value).ToArray() : Array.Empty<DateTimeOffset>());
            return Task.FromResult<AchievementSnapshot?>(AchievementCatalog.Current.Project(characterId, character.RosterId, times));
        }
    }
}
