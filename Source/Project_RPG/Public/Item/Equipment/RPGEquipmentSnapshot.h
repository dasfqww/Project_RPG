#pragma once

#include "Item/RPGItemRuntimeTypes.h"
#include "Type/RPGEnumTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "RPGEquipmentSnapshot.generated.h"

class AActor;
class URPGItemDefinition;
struct FRPGItemRecord;

/** Owner-only, read-only equipment view. Never used to commit item mutations. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGEquipmentSnapshot
{
	GENERATED_BODY()

	static bool TryBuild(const FRPGItemRecord& Record, const URPGItemDefinition& Definition,
		FRPGEquipmentSnapshot& OutSnapshot);
	static bool IsWeaponSlot(EEquipmentSlotType Slot);
	static bool IsUtilitySlot(EEquipmentSlotType Slot);
	float GetStatValue(FGameplayTag Tag) const;

	UPROPERTY(BlueprintReadOnly)
	FGuid ItemId;
	UPROPERTY(BlueprintReadOnly)
	FPrimaryAssetId DefinitionId;
	UPROPERTY(BlueprintReadOnly)
	int32 DefinitionVersion = 0;
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
	UPROPERTY(BlueprintReadOnly)
	EEquipmentSlotType SlotType = EEquipmentSlotType::None;
	UPROPERTY(BlueprintReadOnly)
	ERPGGladiatorWeaponType WeaponType = ERPGGladiatorWeaponType::Count;
	UPROPERTY(BlueprintReadOnly)
	EWeaponHandType WeaponHandType = EWeaponHandType::Count;
	UPROPERTY(BlueprintReadOnly)
	ERPGGladiatorUtilityType UtilityType = ERPGGladiatorUtilityType::Count;
	UPROPERTY(BlueprintReadOnly)
	TArray<FRPGItemStatValue> StatValues;
	UPROPERTY(BlueprintReadOnly)
	FRPGEquipmentProgressionState EquipmentProgression;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AActor> EquippedActor;
};
