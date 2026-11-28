using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Domain;

public sealed record ProgressionSkill(string SkillTag, int Level, IReadOnlyList<string> Tripods);

public sealed record CharacterProgressionState(
    Guid CharacterId, string ProfileId, string ClassId, long Revision,
    int Experience, int Milestones, IReadOnlyList<ProgressionSkill> Skills,
    IReadOnlyList<string> SkillSlots);

public sealed record SkillConfigurationCommand(
    Guid RequestId, long ExpectedRevision, IReadOnlyList<ProgressionSkill> Skills,
    IReadOnlyList<string> SkillSlots);

public sealed class ProgressionConflictException(string code) : Exception(code);

/// <summary>Shared first-slice dungeon milestones and save protocol. Class-specific rules come from the saved profile.</summary>
public static class FirstSliceProgression
{
    public const string ProfileId = GreatSwordProgression.ProfileId;
    public const string ClassId = GreatSwordProgression.ClassId;
    public const string LegacyClassId = GreatSwordProgression.LegacyClassId;
    public const string RewardVersion = "FirstSlice.v1";
    public const string Training = "Progression.Training";
    public const string Intro1 = "Progression.Intro1";
    public const string Intro2 = "Progression.Intro2";
    public const string Intro3 = "Progression.Intro3";
    public const string Boss = "Progression.FirstBoss";
    public const string Assault = GreatSwordProgression.Assault;
    public const string Charge = GreatSwordProgression.Charge;
    public const string Whirlwind = GreatSwordProgression.Whirlwind;
    public const string Jump = GreatSwordProgression.Jump;
    public const string SwordWave = GreatSwordProgression.SwordWave;
    public const string QuickSlash = GreatSwordProgression.QuickSlash;
    public const string CounterSlash = GreatSwordProgression.CounterSlash;
    public const string RetreatSlash = GreatSwordProgression.RetreatSlash;
    private const int Intro2Cleared = 4;
    private const int Intro3Cleared = 8;
    private const int NormalBossCleared = 16;
    private const int HardBossCleared = 32;
    public const long MaximumRevision = 9_007_199_254_740_991;
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    private static CharacterProgressionProfile Rules(CharacterProgressionState state, bool allowLegacy = false) =>
        CharacterProgressionProfiles.Registered.Resolve(state, allowLegacy);

    public static CharacterProgressionState Create(Guid characterId) => GreatSwordProgression.Rules.Create(characterId);
    public static int Level(CharacterProgressionState state) => state.Experience / 100 + 1;
    public static int TotalSP(CharacterProgressionState state) => Rules(state, true).TotalSP(state);
    public static int UsedSP(CharacterProgressionState state) => Rules(state, true).UsedSP(state);
    public static bool IdentityUnlocked(CharacterProgressionState state) => Rules(state, true).IdentityUnlocked(state);
    public static int Cost(int level) => GreatSwordProgression.Rules.Cost(level);

    public static bool IsContent(string dungeonId) => dungeonId is Training or Intro1 or Intro2 or Intro3 or Boss;

    internal static (int Flag, int Required)? Stage(string dungeonId, string difficulty) =>
        (dungeonId, difficulty) switch
        {
            (Training, "Normal") => (1, 0),
            (Intro1, "Normal") => (2, 1),
            (Intro2, "Normal") => (Intro2Cleared, 3),
            (Intro3, "Normal") => (Intro3Cleared, 7),
            (Boss, "Normal") => (NormalBossCleared, 15),
            (Boss, "Hard") => (HardBossCleared, 31),
            _ => null
        };

    public static bool CanEnter(CharacterProgressionState? state, string dungeonId, string difficulty)
    {
        if (!IsContent(dungeonId)) return true;
        return state is not null && Stage(dungeonId, difficulty) is { } stage
            && (state.Milestones & stage.Required) == stage.Required;
    }

    public static CharacterProgressionState MigrateStoredState(CharacterProgressionState state) =>
        Rules(state, true).MigrateStoredState(state);

    public static bool TryCredit(CharacterProgressionState state, string dungeonId, string difficulty,
        string rewardVersion, out CharacterProgressionState result) =>
        Rules(state).TryCredit(state, dungeonId, difficulty, rewardVersion, out result);

    public static CharacterProgressionState Configure(CharacterProgressionState state, SkillConfigurationCommand command) =>
        Rules(state).Configure(state, command);

    public static void ValidateState(CharacterProgressionState state, bool allowLegacyState = false) =>
        Rules(state, allowLegacyState).ValidateState(state, allowLegacyState);

    public static CharacterProgressionState Copy(CharacterProgressionState state) => state with
    {
        Skills = state.Skills.Select(skill => skill with { Tripods = skill.Tripods.ToArray() }).ToArray(),
        SkillSlots = state.SkillSlots.ToArray()
    };

    public static string Fingerprint(SkillConfigurationCommand command)
        => Fingerprint(command, CharacterProgressionProfiles.Registered.AcceptsSkillCount);

    internal static string Fingerprint(SkillConfigurationCommand command, Func<int, bool> acceptsSkillCount)
    {
        if (command.RequestId == Guid.Empty || command.ExpectedRevision is < 1 or > MaximumRevision
            || command.Skills is null || !acceptsSkillCount(command.Skills.Count)
            || command.SkillSlots is null || command.SkillSlots.Count != 8
            || command.Skills.Any(skill => skill is null || string.IsNullOrEmpty(skill.SkillTag) || skill.SkillTag.Length > 96
                || skill.Tripods is null || skill.Tripods.Count != 2
                || skill.Tripods.Any(choice => choice is null || choice.Length > 32))
            || command.SkillSlots.Any(tag => tag is null || tag.Length > 96))
            throw new ProgressionConflictException("invalid_skill_configuration");
        string payload = JsonSerializer.Serialize(new
        {
            command.ExpectedRevision,
            Skills = command.Skills.OrderBy(skill => skill.SkillTag, StringComparer.Ordinal).ToArray(),
            command.SkillSlots
        }, Json);
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(payload))).ToLowerInvariant();
    }

    internal static long NextRevision(long revision) => revision < MaximumRevision
        ? revision + 1 : throw new ProgressionConflictException("progression_revision_exhausted");
}
