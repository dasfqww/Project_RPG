namespace ProjectRpg.Backend.Domain;

public sealed record CharacterCreationOption(string ClassId, string DisplayName, string Description);

/// <summary>Only creation recipes implemented by the current storage providers are offered.</summary>
public static class CharacterCreationCatalog
{
    public const string LegacyDefaultClassId = FirstSliceProgression.ClassId;
    public static IReadOnlyList<CharacterCreationOption> Options { get; } = Array.AsReadOnly(new[]
    {
        new CharacterCreationOption(FirstSliceProgression.ClassId, "대검 전사",
            "대검의 차지 공격과 회전 공격으로 빈틈을 공략하는 근접 전투 직업입니다.")
    });

    // A missing field preserves the existing name-only creation contract.
    // Explicit empty, unknown, draft alias or differently cased IDs are rejected.
    public static bool IsSupported(string? classId) => Options.Any(option =>
        string.Equals(option.ClassId, classId ?? LegacyDefaultClassId, StringComparison.Ordinal));
}
