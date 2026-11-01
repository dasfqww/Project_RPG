namespace ProjectRpg.Backend.Domain;

public sealed record FirstSliceEquipmentDefinition(
    string DefinitionName, FirstSliceEquipmentModel Model, int EquipmentSlotIndex);

/// <summary>Exact authored identities, not prefix/category inference or legacy migration.</summary>
public static class FirstSliceEquipmentDefinitions
{
    public const string DefinitionType = "RPGItemDefinition";
    public const int DefinitionVersion = 1;
    public const string OreDefinitionName = "GameItem.Material.Progression.Ore";
    public const int OreMaximumStackSize = 999;
    public const string TokenDefinitionName = "GameItem.Material.Progression.Token";
    public const int TokenMaximumStackSize = 999;

    public static IReadOnlyList<FirstSliceEquipmentDefinition> All { get; } = Array.AsReadOnly(
        new FirstSliceEquipmentDefinition[]
        {
            new("GameItem.Equipment.Progression.Starter.GreatSword", FirstSliceEquipmentModel.StarterGreatSword, 9),
            new("GameItem.Equipment.Progression.Starter.Chest", FirstSliceEquipmentModel.StarterChest, 1),
            new("GameItem.Equipment.Progression.Starter.Gloves", FirstSliceEquipmentModel.StarterGloves, 4),
            new("GameItem.Equipment.Progression.Target.EnduranceGreatSword", FirstSliceEquipmentModel.EnduranceGreatSword, 9),
            new("GameItem.Equipment.Progression.Target.CirculationGreatSword", FirstSliceEquipmentModel.CirculationGreatSword, 9),
            new("GameItem.Equipment.Progression.Target.Chest", FirstSliceEquipmentModel.TargetChest, 1),
            new("GameItem.Equipment.Progression.Target.Gloves", FirstSliceEquipmentModel.TargetGloves, 4)
        });

    public static bool TryResolve(string definitionType, string definitionName, int definitionVersion,
        out FirstSliceEquipmentDefinition definition)
    {
        definition = null!;
        if (definitionType != DefinitionType || definitionVersion != DefinitionVersion) return false;
        FirstSliceEquipmentDefinition? found = All.FirstOrDefault(entry => entry.DefinitionName == definitionName);
        if (found is null) return false;
        definition = found;
        return true;
    }
}
