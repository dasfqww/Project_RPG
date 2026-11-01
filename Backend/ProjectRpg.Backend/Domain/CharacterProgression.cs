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

/// <summary>The explicit first-slice recipe; it does not change other classes or old saves.</summary>
public static class FirstSliceProgression
{
    public const string ProfileId = "FirstSlice.GreatSword.v1";
    public const string ClassId = "RPGPlayerClassDefinition:GreatSwordWarrior";
    public const string LegacyClassId = "RPGPlayerClassDefinition:GreatSword";
    public const string RewardVersion = "FirstSlice.v1";
    public const string Training = "Progression.Training";
    public const string Intro1 = "Progression.Intro1";
    public const string Intro2 = "Progression.Intro2";
    public const string Intro3 = "Progression.Intro3";
    public const string Boss = "Progression.FirstBoss";
    public const string Assault = "Player.Ability.Skill.AssultBlade";
    public const string Charge = "Player.Ability.Skill.Charge";
    public const string Whirlwind = "Player.Ability.Skill.WhirlWind";
    public const string Jump = "Player.Ability.Skill.JumpSmash";
    public const long MaximumRevision = 9_007_199_254_740_991;
    private static readonly int[] SkillPoints = [0, 3, 6, 9, 12, 14, 15, 16, 17, 18];
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private static readonly IReadOnlyDictionary<string, string[][]> Choices =
        new Dictionary<string, string[][]>(StringComparer.Ordinal)
        {
            [Assault] = [["LongReach", "QuickApproach"], ["ExtraReach", "ExtraRecovery"]],
            [Charge] = [["FullCharge", "QuickCharge"], ["Stagger", "PartBreak"]],
            [Whirlwind] = [["Mobile", "Stationary"], ["WideArea", "FinalStagger"]],
            [Jump] = [["WideArea", "PartBreak"], ["ExtraArea", "Stagger"]]
        };

    public static int Level(CharacterProgressionState state) => state.Experience / 100 + 1;
    public static int TotalSP(CharacterProgressionState state) => SkillPoints[Level(state) - 1];
    public static int Cost(int level) => level <= 4 ? level - 1 : 3 + (level - 4) * 2;
    public static int UsedSP(CharacterProgressionState state) => state.Skills.Sum(skill => Cost(skill.Level));
    public static bool IdentityUnlocked(CharacterProgressionState state) => (state.Milestones & 1) != 0;
    public static bool IsContent(string dungeonId) => dungeonId is Training or Intro1 or Intro2 or Intro3 or Boss;

    private static (int Flag, int Required)? Stage(string dungeonId, string difficulty) =>
        (dungeonId, difficulty) switch
        {
            (Training, "Normal") => (1, 0),
            (Intro1, "Normal") => (2, 1),
            (Intro2, "Normal") => (4, 3),
            (Intro3, "Normal") => (8, 7),
            (Boss, "Normal") => (16, 15),
            (Boss, "Hard") => (32, 31),
            _ => null
        };

    public static bool CanEnter(CharacterProgressionState? state, string dungeonId, string difficulty)
    {
        if (!IsContent(dungeonId)) return true;
        return state is not null && Stage(dungeonId, difficulty) is { } stage
            && (state.Milestones & stage.Required) == stage.Required;
    }

    public static CharacterProgressionState Create(Guid characterId) => new(
        characterId, ProfileId, ClassId, 1, 0, 0,
        [NewSkill(Assault), NewSkill(Charge)], [Assault, Charge, "", "", "", "", "", ""]);

    private static ProgressionSkill NewSkill(string tag) => new(tag, 1, ["", ""]);

    // Explicit correction of the first-slice draft ID to the already-authored class identity.
    // Unknown profiles/classes are never redirected and receipts retain their original result.
    public static CharacterProgressionState MigrateStoredState(CharacterProgressionState state)
    {
        ValidateState(state, allowLegacyClass: true);
        if (state.ClassId != LegacyClassId) return state;
        CharacterProgressionState migrated = state with { ClassId = ClassId, Revision = NextRevision(state.Revision) };
        ValidateState(migrated);
        return Copy(migrated);
    }

    private static string[] Acquired(int flags) => new[] { Assault, Charge }
        .Concat((flags & 1) != 0 ? [Whirlwind] : Array.Empty<string>())
        .Concat((flags & 2) != 0 ? [Jump] : Array.Empty<string>()).ToArray();

    public static CharacterProgressionState Copy(CharacterProgressionState state) => state with
    {
        Skills = state.Skills.Select(skill => skill with { Tripods = skill.Tripods.ToArray() }).ToArray(),
        SkillSlots = state.SkillSlots.ToArray()
    };

    /// <summary>Caller supplies only completed, eligible settlement rows and deduplicates their session IDs.</summary>
    public static bool TryCredit(CharacterProgressionState state, string dungeonId, string difficulty,
        string rewardVersion, out CharacterProgressionState result)
    {
        result = state;
        if (rewardVersion != RewardVersion || Stage(dungeonId, difficulty) is not { } stage
            || (state.Milestones & stage.Required) != stage.Required) return false;
        int xp = Math.Min(900, state.Experience + 100);
        int flags = state.Milestones | stage.Flag;
        if (xp == state.Experience && flags == state.Milestones) return true;
        string[] acquired = Acquired(flags);
        result = state with
        {
            Experience = xp,
            Milestones = flags,
            Revision = NextRevision(state.Revision),
            Skills = acquired.Select(tag => state.Skills.SingleOrDefault(skill => skill.SkillTag == tag)
                ?? NewSkill(tag)).ToArray()
            // Newly acquired skills are not automatically inserted into the eight slots.
        };
        ValidateState(result);
        return true;
    }

    public static string Fingerprint(SkillConfigurationCommand command)
    {
        if (command.RequestId == Guid.Empty || command.ExpectedRevision is < 1 or > MaximumRevision
            || command.Skills is null || command.Skills.Count is < 2 or > 4
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

    public static CharacterProgressionState Configure(CharacterProgressionState state, SkillConfigurationCommand command)
    {
        _ = Fingerprint(command);
        if (state.Revision != command.ExpectedRevision)
            throw new ProgressionConflictException("stale_progression_revision");
        CharacterProgressionState result = state with
        {
            Revision = NextRevision(state.Revision), Skills = command.Skills.ToArray(),
            SkillSlots = command.SkillSlots.ToArray()
        };
        ValidateState(result);
        return Copy(result);
    }

    public static void ValidateState(CharacterProgressionState state, bool allowLegacyClass = false)
    {
        if (state.CharacterId == Guid.Empty || state.ProfileId != ProfileId
            || (state.ClassId != ClassId && (!allowLegacyClass || state.ClassId != LegacyClassId))
            || state.Revision is < 1 or > MaximumRevision || state.Experience is < 0 or > 900
            || state.Experience % 100 != 0 || state.Milestones is < 0 or > 63
            || state.Skills is null || state.SkillSlots is null || state.SkillSlots.Count != 8)
            throw new ProgressionConflictException("invalid_progression_state");
        foreach ((int flag, int required) in new[] { (2, 1), (4, 3), (8, 7), (16, 15), (32, 31) })
            if ((state.Milestones & flag) != 0 && (state.Milestones & required) != required)
                throw new ProgressionConflictException("invalid_progression_milestones");
        if (state.Experience < System.Numerics.BitOperations.PopCount((uint)state.Milestones) * 100)
            throw new ProgressionConflictException("invalid_progression_milestones");
        string[] acquired = Acquired(state.Milestones);
        if (state.Skills.Count != acquired.Length
            || state.Skills.Any(skill => skill is null || !acquired.Contains(skill.SkillTag, StringComparer.Ordinal))
            || state.Skills.Select(skill => skill.SkillTag).Distinct(StringComparer.Ordinal).Count() != acquired.Length)
            throw new ProgressionConflictException("invalid_acquired_skills");
        foreach (ProgressionSkill skill in state.Skills)
        {
            if (skill.Level is < 1 or > 7 || skill.Tripods is null || skill.Tripods.Count != 2)
                throw new ProgressionConflictException("invalid_skill_level");
            for (int tier = 0; tier < 2; tier++)
            {
                string choice = skill.Tripods[tier];
                if (choice is null || (choice.Length > 0 && (skill.Level < (tier == 0 ? 4 : 7)
                    || !Choices[skill.SkillTag][tier].Contains(choice, StringComparer.Ordinal))))
                    throw new ProgressionConflictException("invalid_tripod_selection");
            }
        }
        string[] slotted = state.SkillSlots.Where(tag => !string.IsNullOrEmpty(tag)).ToArray();
        if (state.SkillSlots.Any(tag => tag is null) || slotted.Distinct(StringComparer.Ordinal).Count() != slotted.Length
            || slotted.Any(tag => !acquired.Contains(tag, StringComparer.Ordinal)) || UsedSP(state) > TotalSP(state))
            throw new ProgressionConflictException("invalid_sp_or_skill_slots");
    }

    private static long NextRevision(long revision) => revision < MaximumRevision
        ? revision + 1 : throw new ProgressionConflictException("progression_revision_exhausted");
}
