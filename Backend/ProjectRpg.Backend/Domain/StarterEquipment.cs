namespace ProjectRpg.Backend.Domain;

// Keep the authored UE PrimaryAssetId and equipment slot in sync with
// DA_Item_GreatSword and EEquipmentSlotType::Weapon_Primary_R.
public static class StarterEquipment
{
    public const string DefinitionType = "RPGItemDefinition";
    public const string DefinitionName = "GameItem.Equipment.Weapon.GreatSword";
    public const int PrimaryRightWeaponSlot = 9;

    public static ItemRecord Create(Guid characterId, Guid itemId)
    {
        string ownerId = characterId.ToString("D");
        ItemRecord record = new(
            DefinitionType,
            DefinitionName,
            1,
            new ItemOwnerRef(ItemOwnerType.Character, ownerId),
            new ItemLocation(ItemContainerType.Equipment, ownerId, PrimaryRightWeaponSlot),
            new ItemInstanceState(itemId, 0, 1, [], []),
            1,
            ItemLifecycleState.Active,
            new ItemRecordMetadata(ItemBindState.CharacterBound, new ItemDurability(0, 0),
                null, "CharacterCreation", false));
        if (!ItemRecordRules.TryValidateRecord(record, out string error))
        {
            throw new InvalidOperationException($"Starter equipment is invalid: {error}");
        }

        return record;
    }
}
