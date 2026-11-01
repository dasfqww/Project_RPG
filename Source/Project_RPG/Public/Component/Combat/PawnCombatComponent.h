// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "GameplayTagContainer.h"
#include "Type/RPGEnumTypes.h"
#include "PawnCombatComponent.generated.h"

class ARPGWeaponBase;
class URPGAuthoritativeEquipmentComponent;

UENUM(BlueprintType)
enum class EToggleDamageType : uint8
{
	CurrentEquippedWeapon,
	LeftHand,
	RightHand,
	Body
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UPawnCombatComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
public:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		void RegisterSpawnedWeapon(FGameplayTag InWeaponTagToRegister,
			ARPGWeaponBase* InWeaponToRegister, bool bRegisterAsEquippedWeapon = false);

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		ARPGWeaponBase* GetCharacterHasWeaponByTag(FGameplayTag InWeaponTagToGet) const;

	/** Read-only actor compatibility resources; never native item ownership. */
	TArray<ARPGWeaponBase*> GetRegisteredStartupWeapons() const;

	UPROPERTY(BlueprintReadWrite, Category = "RPG|Combat")
		FGameplayTag CurrentEquippedWeaponTag;

	/** Native active-set actor first; startup actor only while no native weapon exists. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
		ARPGWeaponBase* GetCharacterCurrentEquippedWeapon() const;
	/** Persistent equipment owns identity; shared presentation Actor defaults do not. */
	ERPGGladiatorWeaponType GetCharacterCurrentEquippedWeaponType() const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat")
	void ToggleWeaponCollision(bool bShouldEnable, EToggleDamageType
			ToggleDamageType = EToggleDamageType::CurrentEquippedWeapon);


	virtual void OnHitTargetActor(AActor* HitActor);
	virtual void OnWeaponPulledFromTargetActor(AActor* InteractedActor);

	virtual void ToggleCurrentEquippedWeaponCollision(bool bShouldEnable);
	virtual void ToggleBodyCollsionBoxCollision(bool bShouldEnable, EToggleDamageType ToggleDamageType);

	/** Only valid synchronously inside an authoritative swept-hit event. Not replicated/client supplied. */
	bool TryGetCurrentServerWeaponHit(const AActor* Target, FHitResult& OutHit) const;

protected:
	TArray<AActor*> OverlappedActors;
	virtual bool UsesContinuousWeaponTrace() const { return false; }
	void SweepCurrentWeapon();

private:
	friend class FRPGEquipmentCombatCollisionTest;
	friend class FRPGEquipmentCombatAuthorityTest;
	friend class FRPGWeaponSweepTest;

	void BindEquipmentNotifications();
	void HandleEquipmentChanged();
	void BindWeaponEvents(ARPGWeaponBase* Weapon);
	void UnbindWeaponEvents(ARPGWeaponBase* Weapon);
	bool CanDispatchWeaponEvent(const ARPGWeaponBase* Weapon) const;
	void StopWeaponCollision();

	// Runtime actor references, never an inventory/equipment ownership list.
	TMap<FGameplayTag, TWeakObjectPtr<ARPGWeaponBase>> CharacterHasWeaponMap;
	TWeakObjectPtr<ARPGWeaponBase> CollisionWeapon;
	FTransform PreviousWeaponTransform = FTransform::Identity;
	uint64 CollisionWindowSerial = 0;
	uint64 DispatchWindowSerial = 0;
	const FHitResult* DispatchSweepHit = nullptr;
	bool bSweepingWeapon = false;
	TWeakObjectPtr<URPGAuthoritativeEquipmentComponent> ObservedEquipment;
	FDelegateHandle EquipmentSnapshotsChangedHandle;
	FDelegateHandle ActiveEquipStateChangedHandle;
};
