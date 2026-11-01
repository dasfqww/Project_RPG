#pragma once

#include "Ability/RPGAbilitySet.h"
#include "Component/PawnExtensionComponentBase.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Item/Equipment/RPGEquipmentSnapshot.h"
#include "Type/RPGEnumTypes.h"
#include "RPGAuthoritativeEquipmentComponent.generated.h"

class AActor;
struct FStreamableHandle;
class UDataManager;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRPGActiveEquipStateChanged, EEquipState, EEquipState);
DECLARE_MULTICAST_DELEGATE(FOnRPGEquipmentSnapshotsChanged);

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool Reconcile(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TArray<FRPGItemRecord>& Records,
		const UDataManager& DataManager,
		FString* OutError = nullptr);

	bool IsItemApplied(
		EEquipmentSlotType SlotType,
		const FGuid& ItemId) const;
	int32 NumAppliedItems() const { return AppliedBySlot.Num(); }

	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	bool FindSnapshotInSlot(EEquipmentSlotType Slot, FRPGEquipmentSnapshot& OutSnapshot) const;

	bool FindWeaponSnapshot(EWeaponHandType Hand, FRPGEquipmentSnapshot& OutSnapshot) const;
	bool HasWeaponSnapshots() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	EEquipState GetActiveEquipState() const { return ActiveEquipState; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Equipment")
	void SetActiveEquipState(EEquipState State);

	/** Server changes and owner-client RepNotify share this single state notification. */
	FOnRPGActiveEquipStateChanged OnActiveEquipStateChanged;

	/** Read consumers refresh after reconciliation, actor loading, or owner replication. */
	FOnRPGEquipmentSnapshotsChanged OnEquipmentSnapshotsChanged;

private:
	friend class FRPGEquipmentSnapshotRuntimeTest;
	friend class FRPGEquipmentAbilitySnapshotTest;
	friend class FRPGEquipmentTraceSnapshotTest;
	friend class FRPGLegacyEquipmentReadThroughTest;
	friend class FRPGLegacyEquipmentCommandRoutingTest;
	friend class FRPGLegacyEquipmentStateBridgeTest;
	friend class FRPGEquipmentCombatReadTest;
	friend class FRPGEquipmentCombatCollisionTest;
	friend class FRPGEquipmentCombatAuthorityTest;
	friend class FRPGWeaponLoadoutLifecycleTest;
	friend class FRPGWeaponLoadoutAuthorityTest;
	friend class FRPGWeaponLoadoutInputTest;
	friend class FRPGWeaponStanceTest;

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
	void RefreshSnapshotActors();
	void UpdateActorVisibility();
	bool ApplyProgressionStats(float Attack, float MaximumHealth);

	UFUNCTION()
	void OnRep_ActiveEquipState(EEquipState PreviousState);

	UFUNCTION()
	void OnRep_EquipmentSnapshots();

	UPROPERTY(ReplicatedUsing = OnRep_EquipmentSnapshots)
	TArray<FRPGEquipmentSnapshot> EquipmentSnapshots;

	UPROPERTY(ReplicatedUsing = OnRep_ActiveEquipState)
	EEquipState ActiveEquipState = EEquipState::WeaponSet_Primary;

	TMap<EEquipmentSlotType, FRPGAppliedEquipmentRuntime> AppliedBySlot;
	FRPGItemOwnerRef AppliedOwner;
	FActiveGameplayEffectHandle ProgressionEffect;
	TWeakObjectPtr<class URPGAbilitySystemComponent> ProgressionAbilitySystem;
	float AppliedProgressionAttack = 0.f;
	float AppliedProgressionHealth = 0.f;
};
