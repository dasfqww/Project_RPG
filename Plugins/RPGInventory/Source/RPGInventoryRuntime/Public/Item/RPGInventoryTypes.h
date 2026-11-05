#pragma once

#include "CoreMinimal.h"
#include "RPGInventoryTypes.generated.h"

UENUM(BlueprintType)
enum class EItemCategory :uint8
{
	Equip  UMETA(DisplayName = "Equip"),
	Consume UMETA(DisplayName = "Consume"),
	Craft UMETA(DisplayName = "Craft"),
	None
};

UENUM(BlueprintType)
enum class EEquipmentSlotType : uint8
{
    Head,
    Chest,
    Legs,
    Feet,
    Hands,
    Necklace,
    Ring_L,
    Ring_R,
    Weapon_Primary_L,
    Weapon_Primary_R,
    Weapon_Secondary_L,
    Weapon_Secondary_R,
    Utility_1,
    Utility_2,

    Count UMETA(Hidden),
    None UMETA(Hidden)
};
