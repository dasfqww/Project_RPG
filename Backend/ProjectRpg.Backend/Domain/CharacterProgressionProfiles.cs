namespace ProjectRpg.Backend.Domain;

public sealed class CharacterProgressionProfiles(params CharacterProgressionProfile[] profiles)
{
    public static CharacterProgressionProfiles Registered { get; } = new(GreatSwordProgression.Rules);

    public CharacterProgressionProfile Resolve(CharacterProgressionState state, bool allowLegacy = false) =>
        profiles.SingleOrDefault(profile => profile.Matches(state, allowLegacy))
        ?? throw new ProgressionConflictException("invalid_progression_state");

    internal bool AcceptsSkillCount(int count) => profiles.Any(profile => profile.AcceptsSkillCount(count));
}
