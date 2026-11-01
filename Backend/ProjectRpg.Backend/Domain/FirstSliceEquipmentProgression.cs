namespace ProjectRpg.Backend.Domain;

// Logical recipe entries, not Item Definition IDs. A command handler must resolve
// an owned record through an explicit server-side Definition ID/version allowlist.
public enum FirstSliceEquipmentModel
{
    None,
    StarterGreatSword,
    StarterChest,
    StarterGloves,
    EnduranceGreatSword,
    CirculationGreatSword,
    TargetChest,
    TargetGloves
}

public enum FirstSliceEquipmentSlot
{
    GreatSword,
    Chest,
    Gloves
}

// Optional per-instance growth persisted through ItemRecord/JSONB.
// Legacy records remain uninitialized; no automatic equipment migration is performed.
public sealed record FirstSliceEquipmentProgressionState(
    string GrowthBandId, int RuleVersion, int Level);

public readonly record struct FirstSliceEquipmentContribution(int AttackPower, int MaximumHealth);

public sealed record FirstSliceEquipmentUpgradeStep(
    FirstSliceEquipmentProgressionState NextState,
    int OreCost,
    FirstSliceEquipmentContribution Contribution);

public sealed record FirstSliceEquipmentTransferStep(
    FirstSliceEquipmentProgressionState SourceState,
    FirstSliceEquipmentProgressionState TargetState);

public sealed class EquipmentProgressionRuleException(string code) : Exception(code)
{
    public string Code { get; } = code;
}

/// <summary>
/// Pure first-slice recipe: deterministic +0..+3, whole-level transfer, fixed stats.
/// Does not authorize an item, debit ore, commit records, or apply GAS effects.
/// </summary>
public static class FirstSliceEquipmentProgression
{
    public const string GrowthBandId = "FirstSlice";
    public const int RuleVersion = 1;
    public const int MaximumLevel = 3;

    public static FirstSliceEquipmentProgressionState Create() => new(GrowthBandId, RuleVersion, 0);

    public static bool IsValidState(FirstSliceEquipmentProgressionState? state) => state is not null
        && string.Equals(state.GrowthBandId, GrowthBandId, StringComparison.Ordinal)
        && state.RuleVersion == RuleVersion && state.Level is >= 0 and <= MaximumLevel;

    public static FirstSliceEquipmentSlot Slot(FirstSliceEquipmentModel model) => model switch
    {
        FirstSliceEquipmentModel.StarterGreatSword or FirstSliceEquipmentModel.EnduranceGreatSword
            or FirstSliceEquipmentModel.CirculationGreatSword => FirstSliceEquipmentSlot.GreatSword,
        FirstSliceEquipmentModel.StarterChest or FirstSliceEquipmentModel.TargetChest => FirstSliceEquipmentSlot.Chest,
        FirstSliceEquipmentModel.StarterGloves or FirstSliceEquipmentModel.TargetGloves => FirstSliceEquipmentSlot.Gloves,
        _ => throw new EquipmentProgressionRuleException("unsupported_equipment_model")
    };

    public static FirstSliceEquipmentContribution Contribution(
        FirstSliceEquipmentModel model, FirstSliceEquipmentProgressionState state)
    {
        Validate(state);
        FirstSliceEquipmentSlot slot = Slot(model);
        bool starter = model is FirstSliceEquipmentModel.StarterGreatSword
            or FirstSliceEquipmentModel.StarterChest or FirstSliceEquipmentModel.StarterGloves;
        return slot switch
        {
            FirstSliceEquipmentSlot.GreatSword => new((starter ? 0 : 10) + state.Level * 3, 0),
            FirstSliceEquipmentSlot.Chest => new(0, (starter ? 0 : 100) + state.Level * 40),
            FirstSliceEquipmentSlot.Gloves => new((starter ? 0 : 4) + state.Level * 2, 0),
            _ => throw new EquipmentProgressionRuleException("unsupported_equipment_model")
        };
    }

    public static FirstSliceEquipmentUpgradeStep Upgrade(
        FirstSliceEquipmentModel model, FirstSliceEquipmentProgressionState state)
    {
        Validate(state);
        _ = Slot(model);
        if (state.Level == MaximumLevel)
            throw new EquipmentProgressionRuleException("equipment_at_maximum_level");

        FirstSliceEquipmentProgressionState next = state with { Level = state.Level + 1 };
        return new(next, next.Level * 2, Contribution(model, next));
    }

    public static FirstSliceEquipmentTransferStep Transfer(
        FirstSliceEquipmentModel sourceModel, FirstSliceEquipmentProgressionState source,
        FirstSliceEquipmentModel targetModel, FirstSliceEquipmentProgressionState target)
    {
        Validate(source);
        Validate(target);
        if (Slot(sourceModel) != Slot(targetModel))
            throw new EquipmentProgressionRuleException("equipment_slot_mismatch");
        if (source.Level == 0)
            throw new EquipmentProgressionRuleException("transfer_source_not_enhanced");
        if (target.Level != 0)
            throw new EquipmentProgressionRuleException("transfer_target_already_enhanced");

        // Only the level moves. The caller retains each item's identity, owner,
        // definition, location, bind state, other stats, and revision/CAS context.
        return new(source with { Level = 0 }, target with { Level = source.Level });
    }

    private static void Validate(FirstSliceEquipmentProgressionState state)
    {
        if (state is null)
            throw new EquipmentProgressionRuleException("missing_equipment_progression");
        if (!string.Equals(state.GrowthBandId, GrowthBandId, StringComparison.Ordinal))
            throw new EquipmentProgressionRuleException("unsupported_equipment_growth_band");
        if (state.RuleVersion != RuleVersion)
            throw new EquipmentProgressionRuleException("unsupported_equipment_rule_version");
        if (state.Level < 0 || state.Level > MaximumLevel)
            throw new EquipmentProgressionRuleException("invalid_equipment_level");
    }
}
