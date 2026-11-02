#pragma once

#include "Component/PawnExtensionComponentBase.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "RPGPlayerWeaponLoadoutComponent.generated.h"

class ARPGPlayerWeapon;
class URPGAbilitySystemComponent;
class URPGAuthoritativeEquipmentComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class USkeletalMeshComponent;
class URPGPlayerLinkedAnimLayer;
class UAnimInstance;
class UEnhancedPlayerInput;
class UGameplayAbility;
class UAnimMontage;
struct FGameplayEventData;

UENUM()
enum class ERPGWeaponCarryState : uint8
{
	Stowed,
	Drawn,
	Sheathing
};

/** Public appearance only: no persistent item IDs, stats, or inventory records. */
USTRUCT()
struct FRPGPlayerWeaponPresentation
{
	GENERATED_BODY()
	UPROPERTY()
	bool bNativeManaged = false;
	UPROPERTY()
	TObjectPtr<ARPGPlayerWeapon> ActiveWeapon;
	UPROPERTY()
	ERPGWeaponCarryState CarryState = ERPGWeaponCarryState::Stowed;
};

/** Owns the active weapon's runtime loadout, never item ownership or transactions. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGPlayerWeaponLoadoutComponent final : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
public:
	URPGPlayerWeaponLoadoutComponent();
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Called after possession/startup grants and whenever native equipment changes. */
	void RefreshLoadout();
	/** Rebind after the owning client's input/controller becomes available. */
	UFUNCTION()
	void RefreshPresentation();
	bool IsNativeManaged() const { return Presentation.bNativeManaged; }
	/** Called only for accepted GAS attacks and each actual attack montage/combination step. */
	void NotifyAttackStarted(const UGameplayAbility* Ability);
	static bool IsAttackAbility(const UGameplayAbility* Ability);

private:
	friend class FRPGWeaponLoadoutLifecycleTest;
	friend class FRPGWeaponLoadoutAuthorityTest;
	friend class FRPGWeaponLoadoutInputTest;
	friend class FRPGWeaponStanceTest;
	friend class FRPGWeaponAutoStowTest;
	void ApplyCarryPresentation();
	void ScheduleAutoStow(float Delay);
	void TryAutoStow();
	void HandleStowEvent(const FGameplayEventData* Data);
	void HandleStowMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void ReleaseAutoStow();
	void BindEquipment();
	void BindAnimation();
	void UnbindAnimation();
	bool ResolveGrantLevel(const URPGAbilitySystemComponent& ASC, int32& OutLevel) const;
	bool GrantWeaponAbilities(ARPGPlayerWeapon& Weapon, URPGAbilitySystemComponent& ASC, int32 Level);
	void ReleaseAbilities();
	void RetireStartupResources();
	void RestoreStartupVisibility();
	void ReleasePresentation();
	void ApplyInputContext(UEnhancedInputLocalPlayerSubsystem* Subsystem, UInputMappingContext* Context);
	void ReleaseInputContext();
	void RetireStartupPresentation();

	UFUNCTION()
	void OnRep_Presentation();

	UPROPERTY(ReplicatedUsing = OnRep_Presentation)
	FRPGPlayerWeaponPresentation Presentation;
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Loadout|Auto Stow")
	TSoftObjectPtr<UAnimMontage> StowMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PlayingStowMontage;
	FTimerHandle AutoStowTimer;
	TWeakObjectPtr<URPGAbilitySystemComponent> EventASC;
	FDelegateHandle StowEventHandle;

	/** Retains the existing equip ability's runtime level instead of inventing a balance level. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Loadout", meta = (Categories = "InputTag"))
	FGameplayTag AbilityLevelSourceInputTag;

	TWeakObjectPtr<URPGAuthoritativeEquipmentComponent> ObservedEquipment;
	FDelegateHandle SnapshotChangedHandle;
	FDelegateHandle StateChangedHandle;
	TWeakObjectPtr<URPGAbilitySystemComponent> GrantedASC;
	TWeakObjectPtr<ARPGPlayerWeapon> GrantedWeapon;
	FGuid GrantedItemId;
	int32 GrantedLevel = 0;
	TArray<FGameplayAbilitySpecHandle> GrantedHandles;
	TMap<TWeakObjectPtr<ARPGPlayerWeapon>, bool> StartupVisibility;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	TWeakObjectPtr<UEnhancedPlayerInput> BoundPlayerInput;
	TWeakObjectPtr<UInputMappingContext> InputContext;
	bool bHadPreviousInputContext = false;
	int32 PreviousInputPriority = 0;
	TWeakObjectPtr<USkeletalMeshComponent> LinkedMesh;
	TWeakObjectPtr<USkeletalMeshComponent> ObservedMesh;
	TWeakObjectPtr<UAnimInstance> LinkedAnimInstance;
	TSubclassOf<URPGPlayerLinkedAnimLayer> LinkedLayer;
	TWeakObjectPtr<UAnimInstance> RetiredStartupAnimInstance;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> RetiredStartupInputSubsystem;
	bool bRefreshing = false;
	bool bRefreshingPresentation = false;
};
