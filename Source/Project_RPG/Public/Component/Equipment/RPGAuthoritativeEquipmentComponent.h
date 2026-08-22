#pragma once

#include "Ability/RPGAbilitySet.h"
#include "Component/PawnExtensionComponentBase.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Type/RPGEnumTypes.h"
#include "RPGAuthoritativeEquipmentComponent.generated.h"

class AActor;
struct FStreamableHandle;
class UDataManager;

/** Runtime resources applied for one authoritative equipment record. */
struct FRPGAppliedEquipmentRuntime
{
	FGuid ItemId;
	FPrimaryAssetId DefinitionId;
	int32 DefinitionVersion = 0;
	int64 Revision = 0;
	TArray<FRPGAbilitySet_GrantedHandles> GrantedAbilitySetHandles;
	TObjectPtr<AActor> SpawnedActor;
	TSharedPtr<FStreamableHandle> PendingActorLoad;
};

/**
 * Server-side idempotent reconciler from persistent Equipment records to GAS
 * grants and equipped world actors. Legacy URPGItemBase state is not used.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGAuthoritativeEquipmentComponent final
	: public UPawnExtensionComponentBase
{
	GENERATED_BODY()

public:
	URPGAuthoritativeEquipmentComponent();

	virtual void EndPlay(
		const EEndPlayReason::Type EndPlayReason) override;

	bool Reconcile(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TArray<FRPGItemRecord>& Records,
		const UDataManager& DataManager,
		FString* OutError = nullptr);

	bool IsItemApplied(
		EEquipmentSlotType SlotType,
		const FGuid& ItemId) const;
	int32 NumAppliedItems() const { return AppliedBySlot.Num(); }

private:
	bool ApplyRecord(
		EEquipmentSlotType SlotType,
		const FRPGItemRecord& Record,
		const UDataManager& DataManager,
		FString* OutError);
	void RemoveApplied(EEquipmentSlotType SlotType);
	void ResetApplied();
	void RequestOrSpawnActor(
		EEquipmentSlotType SlotType,
		const FGuid& ItemId,
		const UDataManager& DataManager);
	void HandleActorClassLoaded(
		EEquipmentSlotType SlotType,
		FGuid ItemId);

	TMap<EEquipmentSlotType, FRPGAppliedEquipmentRuntime> AppliedBySlot;
};
