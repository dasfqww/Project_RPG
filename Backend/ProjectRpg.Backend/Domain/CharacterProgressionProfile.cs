using System.Collections.Immutable;

namespace ProjectRpg.Backend.Domain;

public sealed record ProgressionSkillRule(string Tag, int RequiredMilestone,
    ImmutableArray<ImmutableArray<string>> Refinements, bool BackfillLegacy = false);

/// <summary>Immutable class rules for the first-slice campaign and its eight-slot save format.</summary>
public sealed class CharacterProgressionProfile(
    string profileId, string classId, string? legacyClassId,
    ImmutableArray<int> skillPoints, ImmutableArray<int> levelCosts, ImmutableArray<int> refinementLevels,
    int identityMilestone, ImmutableArray<ProgressionSkillRule> skills)
{
    public string ProfileId { get; } = profileId;
    public string ClassId { get; } = classId;
    private readonly Dictionary<string, ProgressionSkillRule> byTag = skills.ToDictionary(skill => skill.Tag, StringComparer.Ordinal);
    private readonly int startingCount = skills.Count(skill => skill.RequiredMilestone == 0);
    private int MaxExperience => (skillPoints.Length - 1) * 100;

    internal bool Matches(CharacterProgressionState state, bool allowLegacy) =>
        state.ProfileId == ProfileId && (state.ClassId == ClassId ||
            (allowLegacy && legacyClassId is not null && state.ClassId == legacyClassId));

    internal bool AcceptsSkillCount(int count) => count >= startingCount && count <= skills.Length;
    public int TotalSP(CharacterProgressionState state) => skillPoints[FirstSliceProgression.Level(state) - 1];
    public int Cost(int level) => levelCosts[level - 1];
    public int UsedSP(CharacterProgressionState state) => state.Skills.Sum(skill => Cost(skill.Level));
    public bool IdentityUnlocked(CharacterProgressionState state) => (state.Milestones & identityMilestone) == identityMilestone;

    public CharacterProgressionState Create(Guid characterId)
    {
        string[] starting = Acquired(0);
        string[] slots = Enumerable.Repeat("", 8).ToArray();
        Array.Copy(starting, slots, Math.Min(starting.Length, slots.Length));
        return new(characterId, ProfileId, ClassId, 1, 0, 0, starting.Select(NewSkill).ToArray(), slots);
    }

    private static ProgressionSkill NewSkill(string tag) => new(tag, 1, ["", ""]);

    private string[] Acquired(int flags, IReadOnlyList<ProgressionSkill>? legacySkills = null) =>
        skills.Where(rule => (flags & rule.RequiredMilestone) == rule.RequiredMilestone &&
            (!rule.BackfillLegacy || legacySkills is null || legacySkills.Any(skill => skill?.SkillTag == rule.Tag)))
            .Select(rule => rule.Tag).ToArray();

    // Migrate live records only. Replayed receipts keep their original state and revision.
    public CharacterProgressionState MigrateStoredState(CharacterProgressionState state)
    {
        ValidateState(state, allowLegacyState: true);
        var missing = skills.Where(rule => rule.BackfillLegacy && (state.Milestones & rule.RequiredMilestone) == rule.RequiredMilestone
                && !state.Skills.Any(skill => skill.SkillTag == rule.Tag))
            .Select(rule => NewSkill(rule.Tag)).ToArray();
        if (state.ClassId == ClassId && missing.Length == 0) return state;
        CharacterProgressionState migrated = state with
        {
            ClassId = ClassId, Revision = FirstSliceProgression.NextRevision(state.Revision),
            Skills = [.. state.Skills, .. missing]
        };
        ValidateState(migrated);
        return FirstSliceProgression.Copy(migrated);
    }

    /// <summary>Caller supplies only completed, eligible settlement rows and deduplicates their session IDs.</summary>
    public bool TryCredit(CharacterProgressionState state, string dungeonId, string difficulty,
        string rewardVersion, out CharacterProgressionState result)
    {
        result = state;
        if (!Matches(state, false)) throw new ProgressionConflictException("invalid_progression_state");
        if (rewardVersion != FirstSliceProgression.RewardVersion || FirstSliceProgression.Stage(dungeonId, difficulty) is not { } stage
            || (state.Milestones & stage.Required) != stage.Required) return false;
        int xp = Math.Min(MaxExperience, state.Experience + 100);
        int flags = state.Milestones | stage.Flag;
        if (xp == state.Experience && flags == state.Milestones) return true;
        string[] acquired = Acquired(flags);
        result = state with
        {
            Experience = xp,
            Milestones = flags,
            Revision = FirstSliceProgression.NextRevision(state.Revision),
            Skills = acquired.Select(tag => state.Skills.SingleOrDefault(skill => skill.SkillTag == tag)
                ?? NewSkill(tag)).ToArray()
            // Newly acquired skills are not automatically inserted into the eight slots.
        };
        ValidateState(result);
        return true;
    }

    public CharacterProgressionState Configure(CharacterProgressionState state, SkillConfigurationCommand command)
    {
        _ = FirstSliceProgression.Fingerprint(command, AcceptsSkillCount);
        if (state.Revision != command.ExpectedRevision)
            throw new ProgressionConflictException("stale_progression_revision");
        CharacterProgressionState result = state with
        {
            Revision = FirstSliceProgression.NextRevision(state.Revision), Skills = command.Skills.ToArray(),
            SkillSlots = command.SkillSlots.ToArray()
        };
        ValidateState(result);
        return FirstSliceProgression.Copy(result);
    }

    public void ValidateState(CharacterProgressionState state, bool allowLegacyState = false)
    {
        if (state.CharacterId == Guid.Empty || !Matches(state, allowLegacyState)
            || state.Revision is < 1 or > FirstSliceProgression.MaximumRevision || state.Experience < 0 || state.Experience > MaxExperience
            || state.Experience % 100 != 0 || state.Milestones is < 0 or > 63
            || state.Skills is null || state.SkillSlots is null || state.SkillSlots.Count != 8)
            throw new ProgressionConflictException("invalid_progression_state");
        foreach ((int flag, int required) in new[] { (2, 1), (4, 3), (8, 7), (16, 15), (32, 31) })
            if ((state.Milestones & flag) != 0 && (state.Milestones & required) != required)
                throw new ProgressionConflictException("invalid_progression_milestones");
        if (state.Experience < Math.Min(MaxExperience, System.Numerics.BitOperations.PopCount((uint)state.Milestones) * 100))
            throw new ProgressionConflictException("invalid_progression_milestones");
        string[] acquired = Acquired(state.Milestones, allowLegacyState ? state.Skills : null);
        if (state.Skills.Count != acquired.Length
            || state.Skills.Any(skill => skill is null || !acquired.Contains(skill.SkillTag, StringComparer.Ordinal))
            || state.Skills.Select(skill => skill.SkillTag).Distinct(StringComparer.Ordinal).Count() != acquired.Length)
            throw new ProgressionConflictException("invalid_acquired_skills");
        foreach (ProgressionSkill skill in state.Skills)
        {
            if (skill.Level < 1 || skill.Level > levelCosts.Length || skill.Tripods is null || skill.Tripods.Count != 2)
                throw new ProgressionConflictException("invalid_skill_level");
            for (int tier = 0; tier < 2; tier++)
            {
                string choice = skill.Tripods[tier];
                if (choice is null || (choice.Length > 0 && (skill.Level < refinementLevels[tier]
                    || !byTag[skill.SkillTag].Refinements[tier].Contains(choice, StringComparer.Ordinal))))
                    throw new ProgressionConflictException("invalid_tripod_selection");
            }
        }
        string[] slotted = state.SkillSlots.Where(tag => !string.IsNullOrEmpty(tag)).ToArray();
        if (state.SkillSlots.Any(tag => tag is null) || slotted.Distinct(StringComparer.Ordinal).Count() != slotted.Length
            || slotted.Any(tag => !acquired.Contains(tag, StringComparer.Ordinal)) || UsedSP(state) > TotalSP(state))
            throw new ProgressionConflictException("invalid_sp_or_skill_slots");
    }
}
