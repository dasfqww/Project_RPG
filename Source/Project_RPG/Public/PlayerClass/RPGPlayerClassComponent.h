#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "RPGPlayerClassComponent.generated.h"

class URPGAbilitySystemComponent;
class URPGGameplayAbility;
class URPGPlayerSkillComponent;
class URPGPlayerClassDefinition;

DECLARE_MULTICAST_DELEGATE(FOnRPGPlayerClassApplicationChanged);

UENUM(BlueprintType)
enum class ERPGPlayerClassApplyState : uint8
{
	Waiting,
	Applying,
	Applied,
	Failed,
	Released
};

/** The single ownership list for this component's current Pawn/ASC grants. */
USTRUCT()
struct FRPGPlayerClassAbilityGrant
{
	GENERATED_BODY()
	UPROPERTY()
	FGameplayAbilitySpecHandle Handle;
	UPROPERTY()
	TSubclassOf<URPGGameplayAbility> AbilityClass;
};

/** Applies verified progression to GAS. Does not own growth, equipment, slots or class execution. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGPlayerClassComponent final : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
public:
	URPGPlayerClassComponent();
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Called after common startup finishes. Grant level is supplied by the existing startup owner. */
	void NotifyAbilitySystemReady(URPGAbilitySystemComponent& ASC, int32 GrantLevel);
	bool IsAbilitySystemReady() const;
	/** Lets trusted record suppliers wait for startup/application without polling or owning grant state. */
	FOnRPGPlayerClassApplicationChanged OnApplicationChanged;
	void RefreshAppliedClass();
	/** Terminal for this Pawn. Only this component's own handles are cancelled/removed. */
	void ReleaseClassAbilities();

	/** Used by existing startup/weapon owners before they grant or remove anything. */
	bool RoutesAbilityToClass(const UClass* AbilityClass, FGameplayTag InputTag = FGameplayTag()) const;
	void ReserveStartupIdentity(TSubclassOf<URPGGameplayAbility> AbilityClass);
	bool OwnsAbilitySpec(FGameplayAbilitySpecHandle Handle, const UClass* AbilityClass) const;
	bool CanUseAbilitySpec(FGameplayAbilitySpecHandle Handle, const UClass* AbilityClass) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Class")
	ERPGPlayerClassApplyState GetApplyState() const { return ApplyState; }
	const FText& GetLastApplyError() const { return LastApplyError; }

private:
	void BindProgression();
	void UnbindProgression();
	void RemoveOwnedGrants();
	bool IsContextCurrent(const URPGAbilitySystemComponent* ASC, const URPGPlayerClassDefinition* Definition) const;
	bool ValidateOwnedGrants(const URPGAbilitySystemComponent& ASC) const;
	void FailApplication(const FText& Error);
	UFUNCTION()
	void HandleProgressionChanged(FGameplayTag SkillTag);

	UPROPERTY(Replicated)
	ERPGPlayerClassApplyState ApplyState = ERPGPlayerClassApplyState::Waiting;
	UPROPERTY(Replicated)
	TArray<FRPGPlayerClassAbilityGrant> OwnedGrants;
	UPROPERTY(Transient)
	FText LastApplyError;
	UPROPERTY(Transient)
	TSubclassOf<URPGGameplayAbility> ReservedStartupIdentity;

	TWeakObjectPtr<URPGPlayerSkillComponent> ObservedProgression;
	TWeakObjectPtr<URPGAbilitySystemComponent> ReadyASC;
	TWeakObjectPtr<URPGPlayerClassDefinition> AppliedDefinition;
	int32 AbilityGrantLevel = 0;
	bool bApplying = false;
	bool bHasAppliedOnce = false;
	bool bEndingPlay = false;
};
