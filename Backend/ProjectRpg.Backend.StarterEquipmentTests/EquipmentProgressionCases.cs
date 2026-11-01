using ProjectRpg.Backend.Domain;
using Rules = ProjectRpg.Backend.Domain.FirstSliceEquipmentProgression;

internal static class EquipmentProgressionCases
{
    public static void Run()
    {
        List<string> groups = [];
        FirstSliceEquipmentModel[] models = Enum.GetValues<FirstSliceEquipmentModel>()
            .Where(model => model != FirstSliceEquipmentModel.None).ToArray();
        foreach (FirstSliceEquipmentModel model in models)
        {
            FirstSliceEquipmentProgressionState state = Rules.Create();
            int spent = 0;
            foreach (int expectedCost in new[] { 2, 4, 6 })
            {
                int before = state.Level;
                FirstSliceEquipmentUpgradeStep step = Rules.Upgrade(model, state);
                Require(step.OreCost == expectedCost && step.NextState.Level == before + 1
                    && state.Level == before, "Upgrade must advance once without changing its input.");
                Require(step.Contribution == Rules.Contribution(model, step.NextState), "Upgrade stats differ from the recipe.");
                spent += step.OreCost;
                state = step.NextState;
            }
            Require(spent == 12, "Each +3 item must cost twelve ore.");
            Reject(() => Rules.Upgrade(model, state), "equipment_at_maximum_level");
        }
        groups.Add("all_models_upgrade_costs_and_cap");

        // Check the numeric design's externally specified loadouts, including the
        // target +0 bases and the starter armor used for Hard preparation.
        Require(Loadout(false, 0, false, 0, false, 0) == new FirstSliceEquipmentContribution(100, 1000), "Starter baseline differs.");
        Require(Loadout(false, 2, false, 0, false, 0) == new FirstSliceEquipmentContribution(106, 1000), "Normal entry differs.");
        Require(Loadout(true, 2, false, 2, false, 2) == new FirstSliceEquipmentContribution(120, 1080), "Hard preparation differs.");
        Require(Loadout(true, 2, true, 2, true, 2) == new FirstSliceEquipmentContribution(124, 1180), "Target +2 differs.");
        Require(Loadout(true, 3, true, 3, true, 3) == new FirstSliceEquipmentContribution(129, 1220), "Target +3 differs.");
        Require(Loadout(true, 0, true, 0, true, 0) == new FirstSliceEquipmentContribution(114, 1100), "Target +0 differs.");
        for (int level = 0; level <= 3; level++)
            Require(Rules.Contribution(FirstSliceEquipmentModel.EnduranceGreatSword, State(level))
                == Rules.Contribution(FirstSliceEquipmentModel.CirculationGreatSword, State(level)), "Weapon variants must share numeric stats.");
        groups.Add("documented_loadouts_and_weapon_variant_parity");

        int transferAccepted = 0, transferRejected = 0;
        foreach (FirstSliceEquipmentModel sourceModel in models)
        foreach (FirstSliceEquipmentModel targetModel in models)
        for (int sourceLevel = 0; sourceLevel <= 3; sourceLevel++)
        for (int targetLevel = 0; targetLevel <= 3; targetLevel++)
        {
            FirstSliceEquipmentProgressionState source = State(sourceLevel), target = State(targetLevel);
            string? error = Rules.Slot(sourceModel) != Rules.Slot(targetModel) ? "equipment_slot_mismatch"
                : sourceLevel == 0 ? "transfer_source_not_enhanced"
                : targetLevel != 0 ? "transfer_target_already_enhanced" : null;
            if (error is not null)
            {
                Reject(() => Rules.Transfer(sourceModel, source, targetModel, target), error);
                transferRejected++;
                continue;
            }
            FirstSliceEquipmentTransferStep moved = Rules.Transfer(sourceModel, source, targetModel, target);
            Require(moved.SourceState.Level == 0 && moved.TargetState.Level == sourceLevel
                && source.Level == sourceLevel && target.Level == targetLevel, "Transfer duplicated/lost levels or mutated inputs.");
            FirstSliceEquipmentTransferStep returned = Rules.Transfer(targetModel, moved.TargetState, sourceModel, moved.SourceState);
            Require(returned.SourceState == target && returned.TargetState == source, "Reverse transfer must restore levels at no cost.");
            transferAccepted++;
        }
        Require(transferAccepted == 51 && transferRejected == 733, "The full seven-model transfer matrix was not checked.");
        groups.Add("complete_transfer_matrix_and_round_trip");

        foreach (int invalid in new[] { int.MinValue, -1, 4, int.MaxValue })
            RejectState(State(invalid), "invalid_equipment_level");
        foreach (string invalid in new[] { "", "firstslice", "OtherBand", "FirstSlice " })
            RejectState(Rules.Create() with { GrowthBandId = invalid }, "unsupported_equipment_growth_band");
        foreach (int invalid in new[] { int.MinValue, 0, 2, int.MaxValue })
            RejectState(Rules.Create() with { RuleVersion = invalid }, "unsupported_equipment_rule_version");
        RejectState(null!, "missing_equipment_progression");
        foreach (FirstSliceEquipmentModel invalid in new[] { FirstSliceEquipmentModel.None, (FirstSliceEquipmentModel)(-1), (FirstSliceEquipmentModel)999 })
        {
            Reject(() => Rules.Slot(invalid), "unsupported_equipment_model");
            Reject(() => Rules.Contribution(invalid, State(0)), "unsupported_equipment_model");
            Reject(() => Rules.Upgrade(invalid, State(0)), "unsupported_equipment_model");
            Reject(() => Rules.Transfer(invalid, State(1), models[0], State(0)), "unsupported_equipment_model");
            Reject(() => Rules.Transfer(models[0], State(1), invalid, State(0)), "unsupported_equipment_model");
        }
        groups.Add("unsupported_state_and_model_refusal");

        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new
        {
            passed = true, scope = "Pure equipment rules only; no item repository, API, inventory debit or GAS.",
            groups, models = models.Length, transferAccepted, transferRejected
        }));
    }

    private static FirstSliceEquipmentProgressionState State(int level) => Rules.Create() with { Level = level };

    private static void RejectState(FirstSliceEquipmentProgressionState state, string code)
    {
        Reject(() => Rules.Contribution(FirstSliceEquipmentModel.StarterGreatSword, state), code);
        Reject(() => Rules.Upgrade(FirstSliceEquipmentModel.StarterGreatSword, state), code);
        Reject(() => Rules.Transfer(FirstSliceEquipmentModel.StarterGreatSword, state,
            FirstSliceEquipmentModel.EnduranceGreatSword, State(0)), code);
        Reject(() => Rules.Transfer(FirstSliceEquipmentModel.StarterGreatSword, State(1),
            FirstSliceEquipmentModel.EnduranceGreatSword, state), code);
    }

    private static FirstSliceEquipmentContribution Loadout(bool targetWeapon, int weaponLevel,
        bool targetChest, int chestLevel, bool targetGloves, int glovesLevel)
    {
        FirstSliceEquipmentContribution weapon = Rules.Contribution(targetWeapon ? FirstSliceEquipmentModel.EnduranceGreatSword
            : FirstSliceEquipmentModel.StarterGreatSword, State(weaponLevel));
        FirstSliceEquipmentContribution chest = Rules.Contribution(targetChest ? FirstSliceEquipmentModel.TargetChest
            : FirstSliceEquipmentModel.StarterChest, State(chestLevel));
        FirstSliceEquipmentContribution gloves = Rules.Contribution(targetGloves ? FirstSliceEquipmentModel.TargetGloves
            : FirstSliceEquipmentModel.StarterGloves, State(glovesLevel));
        return new(100 + weapon.AttackPower + gloves.AttackPower, 1000 + chest.MaximumHealth);
    }

    private static void Reject(Action action, string expected)
    {
        try { action(); }
        catch (EquipmentProgressionRuleException error) when (error.Code == expected) { return; }
        throw new InvalidOperationException("Expected equipment rule refusal: " + expected);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
