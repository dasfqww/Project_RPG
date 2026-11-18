using System.Text.Json;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Achievements;

public sealed record AchievementDefinition(string Id, string Title, string Kind, string Subject, string Difficulty, int Target);
public sealed record AchievementTrack(string Kind, string Subject, string Difficulty);
public sealed record AchievementEntry(string Id, string Title, int Target, int Progress, DateTimeOffset? EarnedAt);
public sealed record AchievementSnapshot(Guid CharacterId, Guid RosterId, AchievementEntry[] Entries);

public interface IAchievementRepository
{
    Task<AchievementSnapshot?> ReadAchievementsAsync(Guid characterId, string steamId, CancellationToken ct);
}

public sealed class AchievementCatalog
{
    private readonly AchievementDefinition[] definitions;
    public IReadOnlyDictionary<AchievementTrack, int> Thresholds { get; }
    public static AchievementCatalog Current { get; } = new();

    private AchievementCatalog()
    {
        using var stream = typeof(AchievementCatalog).Assembly.GetManifestResourceStream("Achievements.catalog.json")!;
        definitions = JsonSerializer.Deserialize<AchievementDefinition[]>(stream, new JsonSerializerOptions(JsonSerializerDefaults.Web))!;
        if (definitions.Length == 0 || definitions.Select(d => d.Id).Distinct().Count() != definitions.Length ||
            definitions.Any(d => string.IsNullOrWhiteSpace(d.Id) || string.IsNullOrWhiteSpace(d.Title)
                || string.IsNullOrWhiteSpace(d.Subject) || d.Target is < 1 or > 10000
                || !(d.Kind == "DungeonClear" && !string.IsNullOrWhiteSpace(d.Difficulty)
                    || d.Kind == "ChapterCompleted" && d.Difficulty == "" && d.Target == 1)))
            throw new InvalidDataException("Invalid roster achievement catalog.");
        Thresholds = definitions.GroupBy(Track).ToDictionary(g => g.Key, g => g.Max(d => d.Target));
    }

    private static AchievementTrack Track(AchievementDefinition d) => new(d.Kind, d.Subject, d.Difficulty);

    public AchievementSnapshot Project(Guid character, Guid roster, Dictionary<AchievementTrack, DateTimeOffset[]> facts)
    {
        return new(character, roster, definitions.Select(d =>
        {
            var times = facts[Track(d)];
            return new AchievementEntry(d.Id, d.Title, d.Target, Math.Min(d.Target, times.Length),
                times.Length >= d.Target ? times[d.Target - 1] : null);
        }).ToArray());
    }
}

public static class AchievementEndpoints
{
    public static void MapAchievements(this WebApplication app)
    {
        app.MapGet("/api/characters/{characterId:guid}/achievements", async (Guid characterId, HttpContext context,
            [Microsoft.AspNetCore.Mvc.FromServices] IAchievementRepository repository, CancellationToken ct) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            var principal = context.GetProjectRpgPrincipal();
            if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
                return Results.StatusCode(StatusCodes.Status403Forbidden);
            var snapshot = await repository.ReadAchievementsAsync(characterId, principal.SteamId, ct);
            return snapshot is null ? Results.StatusCode(StatusCodes.Status403Forbidden) : Results.Ok(snapshot);
        });
    }
}
