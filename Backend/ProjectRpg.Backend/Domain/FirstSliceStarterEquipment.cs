namespace ProjectRpg.Backend.Domain;

/// <summary>Explicit +0 first-slice creation recipe; never migrates existing items.</summary>
public static class FirstSliceStarterEquipment
{
    public static IReadOnlyList<ItemRecord> Create(Guid characterId)
    {
        if (characterId == Guid.Empty) throw new ArgumentException("A starter requires a character ID.", nameof(characterId));
        ItemOwnerRef owner = new(ItemOwnerType.Character, characterId.ToString("D"));
        FirstSliceEquipmentModel[] models = [FirstSliceEquipmentModel.StarterGreatSword,
            FirstSliceEquipmentModel.StarterChest, FirstSliceEquipmentModel.StarterGloves];
        List<ItemRecord> records = [];
        foreach (FirstSliceEquipmentModel model in models)
        {
            FirstSliceEquipmentDefinition definition = FirstSliceEquipmentDefinitions.All.Single(entry => entry.Model == model);
            ItemRecord item = new(FirstSliceEquipmentDefinitions.DefinitionType, definition.DefinitionName,
                FirstSliceEquipmentDefinitions.DefinitionVersion, owner,
                new(ItemContainerType.Equipment, owner.OwnerId, definition.EquipmentSlotIndex),
                new(Guid.NewGuid(), 0, 1, [], [], FirstSliceEquipmentProgression.Create()), 1,
                ItemLifecycleState.Active, new(ItemBindState.CharacterBound, new(0, 0), null, "CharacterCreation", false));
            if (!ItemRecordRules.TryValidateRecord(item, out string error))
                throw new InvalidOperationException("Invalid first-slice starter: " + error);
            records.Add(item);
        }
        return records.AsReadOnly();
    }
}
