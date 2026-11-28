namespace ProjectRpg.Backend.Domain;

public static class GreatSwordProgression
{
    public const string ProfileId = "FirstSlice.GreatSword.v1";
    public const string ClassId = "RPGPlayerClassDefinition:GreatSwordWarrior";
    public const string LegacyClassId = "RPGPlayerClassDefinition:GreatSword";
    public const string Assault = "Player.Ability.Skill.AssultBlade";
    public const string Charge = "Player.Ability.Skill.Charge";
    public const string Whirlwind = "Player.Ability.Skill.WhirlWind";
    public const string Jump = "Player.Ability.Skill.JumpSmash";
    public const string SwordWave = "Player.Ability.Skill.SwordWave";
    public const string QuickSlash = "Player.Ability.Skill.QuickSlash";
    public const string CounterSlash = "Player.Ability.Skill.CounterSlash";
    public const string RetreatSlash = "Player.Ability.Skill.RetreatSlash";

    public static CharacterProgressionProfile Rules { get; } = new(
        ProfileId, ClassId, LegacyClassId,
        skillPoints: [0, 3, 6, 9, 12, 14, 15, 16, 17, 18],
        levelCosts: [0, 1, 2, 3, 5, 7, 9], refinementLevels: [4, 7], identityMilestone: 1,
        skills: [
            new(Assault, 0, [["LongReach", "QuickApproach"], ["ExtraReach", "ExtraRecovery"]]),
            new(Charge, 0, [["FullCharge", "QuickCharge"], ["Stagger", "PartBreak"]]),
            new(Whirlwind, 1, [["Mobile", "Stationary"], ["WideArea", "FinalStagger"]]),
            new(Jump, 2, [["WideArea", "PartBreak"], ["ExtraArea", "Stagger"]]),
            new(SwordWave, 4, [[], []], BackfillLegacy: true),
            new(QuickSlash, 8, [[], []], BackfillLegacy: true),
            new(CounterSlash, 16, [[], []], BackfillLegacy: true),
            new(RetreatSlash, 32, [[], []], BackfillLegacy: true)
        ]);
}
