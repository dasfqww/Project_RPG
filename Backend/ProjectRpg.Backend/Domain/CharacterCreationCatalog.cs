namespace ProjectRpg.Backend.Domain;

public sealed record CharacterCreationOption(string ClassId, string DisplayName, string Description);

public sealed record CharacterCreationRecipe(
    CharacterCreationOption Option,
    Func<Guid, CharacterProgressionState> CreateProgression,
    Func<Guid, IReadOnlyList<ItemRecord>> CreateEquipment);

/// <summary>Only creation recipes implemented by the current storage providers are offered.</summary>
public static class CharacterCreationCatalog
{
    public const string LegacyDefaultClassId = FirstSliceProgression.ClassId;
    public static CharacterCreationRecipe Default { get; } = new(
        new CharacterCreationOption(LegacyDefaultClassId, "대검 전사",
            "대검의 차지 공격과 회전 공격으로 빈틈을 공략하는 근접 전투 직업입니다."),
        FirstSliceProgression.Create, FirstSliceStarterEquipment.Create);

    private static readonly CharacterCreationRecipe[] Recipes = [Default];
    public static IReadOnlyList<CharacterCreationOption> Options { get; } =
        Array.AsReadOnly(Recipes.Select(recipe => recipe.Option).ToArray());

    // A missing field preserves the existing name-only creation contract.
    // Explicit empty, unknown, draft alias or differently cased IDs are rejected.
    public static CharacterCreationRecipe? Resolve(string? classId) => Recipes.SingleOrDefault(recipe =>
        string.Equals(recipe.Option.ClassId, classId ?? LegacyDefaultClassId, StringComparison.Ordinal));

    public static bool IsSupported(string? classId) => Resolve(classId) is not null;
}
