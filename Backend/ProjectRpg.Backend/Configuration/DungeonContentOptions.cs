namespace ProjectRpg.Backend.Configuration;

// Operator-owned admission/launch metadata, never supplied by a player request.
public sealed class DungeonContentOptions
{
    public const string SectionName = "DungeonContent";
    public List<DungeonContentEntryOptions?> Entries { get; init; } = [];
}

public sealed class DungeonContentEntryOptions
{
    public string DungeonId { get; init; } = string.Empty;
    public string MapPackage { get; init; } = string.Empty;
    public List<string> Difficulties { get; init; } = [];
    public List<string> RequiredStoryUnlocks { get; init; } = [];
}
