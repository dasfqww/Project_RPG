using System.Collections.Immutable;
using System.Text.RegularExpressions;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Application;

public sealed class DungeonContentCatalog
{
    private readonly Dictionary<DungeonContentSelection, string> maps = [];
    private readonly Dictionary<string, ImmutableArray<string>> storyRequirements = new(StringComparer.Ordinal);
    public IReadOnlyList<DungeonContentSelection> AllowedSelections { get; }

    // A process-lifetime snapshot. Configuration reload cannot change an assignment
    // between the repository's atomic claim and its launch response.
    public DungeonContentCatalog(DungeonContentOptions options)
    {
        if (options.Entries is null || options.Entries.Count is < 1 or > 64)
            throw Invalid("requires 1 to 64 entries");
        HashSet<string> ids = new(StringComparer.Ordinal);
        foreach (DungeonContentEntryOptions? entry in options.Entries)
        {
            if (entry is null || !IsDungeonId(entry.DungeonId) || !ids.Add(entry.DungeonId))
                throw Invalid("contains a null, invalid or duplicate DungeonId");
            if (!IsMapPackage(entry.MapPackage))
                throw Invalid("MapPackage must be an absolute /Game or /Engine package without URL options or object suffix");
            if (entry.Difficulties is null || entry.Difficulties.Count is < 1 or > 4)
                throw Invalid("each entry requires 1 to 4 difficulties");
            if (entry.RequiredStoryUnlocks is null || entry.RequiredStoryUnlocks.Count > 256
                || !entry.RequiredStoryUnlocks.All(NarrativeCommand.ValidId)
                || entry.RequiredStoryUnlocks.Count != entry.RequiredStoryUnlocks.Distinct(StringComparer.Ordinal).Count())
                throw Invalid("RequiredStoryUnlocks contains invalid, duplicate or too many IDs");
            storyRequirements.Add(entry.DungeonId, [.. entry.RequiredStoryUnlocks]);
            foreach (string difficulty in entry.Difficulties)
            {
                if (difficulty is not ("Easy" or "Normal" or "Hard" or "Hell")
                    || !maps.TryAdd(new(entry.DungeonId, difficulty), entry.MapPackage))
                    throw Invalid("contains an unknown or duplicate difficulty");
            }
        }
        AllowedSelections = Array.AsReadOnly(maps.Keys.ToArray());
    }

    public string? GetSelectionError(string? dungeonId, string? difficulty)
    {
        if (dungeonId is null || !maps.Keys.Any(pair => pair.DungeonId == dungeonId))
            return "dungeon_content_not_available";
        return difficulty is null || !maps.ContainsKey(new(dungeonId, difficulty))
            ? "dungeon_difficulty_not_available" : null;
    }

    public string GetMapPackage(string dungeonId, string difficulty) =>
        maps.TryGetValue(new(dungeonId, difficulty), out string? map)
            ? map : throw Invalid("assignment is not in the catalog");

    // Omitted gates preserve legacy content/recovery behavior. The snapshot never
    // aliases operator-owned mutable options or reads reloaded configuration.
    public ImmutableArray<string> GetRequiredStoryUnlocks(string dungeonId) =>
        storyRequirements.GetValueOrDefault(dungeonId, []);

    private static bool IsDungeonId(string? value) => value is { Length: >= 1 and <= 64 }
        && value.All(character => char.IsAsciiLetterOrDigit(character) || character is '.' or '_' or '-');

    private static bool IsMapPackage(string? value) => value is { Length: <= 256 }
        && Regex.IsMatch(value, "\\A/(?:Game|Engine)/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+\\z",
            RegexOptions.CultureInvariant);

    private static InvalidOperationException Invalid(string message) =>
        new($"{DungeonContentOptions.SectionName}: {message}.");
}
