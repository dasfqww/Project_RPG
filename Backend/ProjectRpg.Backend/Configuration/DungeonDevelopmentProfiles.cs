using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using ProjectRpg.Backend.Application;

namespace ProjectRpg.Backend.Configuration;

// Explicit development composition, not an environment-wide or hot-reload catalog.
public static class DungeonDevelopmentProfiles
{
    public const string Setting = "DungeonContent:DevelopmentProfile";

    public static DungeonContentCatalog Resolve(ConfigurationManager configuration,
        IHostEnvironment environment, string baseDirectory)
    {
        string? name = configuration[Setting];
        if (name is null)
            return new DungeonContentCatalog(configuration.GetSection(DungeonContentOptions.SectionName)
                .Get<DungeonContentOptions>() ?? new DungeonContentOptions());

        if (!environment.IsDevelopment() && !environment.IsEnvironment("Testing"))
            throw new InvalidOperationException("Dungeon development profiles require Development or Testing.");
        // A closed set of packaged profiles. Never interpret a requested name as a file path.
        string fileName = name switch
        {
            "Training" => "Training.json",
            "Intro1" => "Intro1.json",
            _ => throw new InvalidOperationException("Unknown dungeon development profile. Supported: Training, Intro1.")
        };

        string path = Path.Combine(baseDirectory, "DevelopmentProfiles", fileName);
        ProfileDocument profile = JsonSerializer.Deserialize<ProfileDocument>(File.ReadAllText(path),
            new JsonSerializerOptions(JsonSerializerDefaults.Web)
            {
                UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
            }) ?? throw new InvalidOperationException("Dungeon development profile is empty.");
        if (profile.SchemaVersion != 1 || profile.Name != name || profile.Content is null
            || profile.MinimumPartySize < 1 || profile.MaximumPartySize > 4
            || profile.MinimumPartySize > profile.MaximumPartySize)
            throw new InvalidOperationException("Invalid dungeon development profile metadata.");

        // Bind the complete document, NOT configuration providers' indexed-array
        // merge. Otherwise default Raid/Easy/Hard entries could leak into a profile.
        DungeonContentCatalog catalog = new(profile.Content);
        // Validate everything before changing the two known process-local settings.
        // Auth, storage, progression, ticket and reward policies are never overridden.
        configuration["DungeonSessions:MinimumDeparturePartySize"] =
            profile.MinimumPartySize.ToString(CultureInfo.InvariantCulture);
        configuration["DungeonSessions:MaxPartySize"] =
            profile.MaximumPartySize.ToString(CultureInfo.InvariantCulture);
        return catalog;
    }

    private sealed class ProfileDocument
    {
        public required int SchemaVersion { get; init; }
        public required string Name { get; init; }
        public required int MinimumPartySize { get; init; }
        public required int MaximumPartySize { get; init; }
        public required DungeonContentOptions Content { get; init; }
    }
}
