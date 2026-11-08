// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Type/RPGStructTypes.h"
#include "Security/RPGTargetDataAdmission.h"
#include "Security/RPGServerRequestBudget.h"
#include "Riding/RPGRidingAbilityGate.h"
#include "RPGAbilitySystemComponent.generated.h"

class ARPGPlayerState;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
public:
	virtual float PlayMontage(UGameplayAbility* AnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo,
		UAnimMontage* Montage, float PlayRate, FName StartSection = NAME_None, float StartTime = 0.f) override;
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
	virtual void ClearActorInfo() override;
	/** Refresh the local controller cache without resetting an existing montage context. */
	void RefreshLocalAbilityActorInfo();

	/** Exact current PlayerState only; retains grants/effects and never destroys the Pawn. */
	bool RevokeDungeonGameplayAuthority(const ARPGPlayerState& RevokedState);
	bool IsDungeonGameplayAuthorityRevoked() const { return bDungeonGameplayAuthorityRevoked; }
	virtual bool AreAbilityTagsBlocked(const FGameplayTagContainer& Tags) const override;
	virtual void NotifyAbilityActivated(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability) override;
	virtual void NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle,
		UGameplayAbility* Ability, bool bWasCancelled) override;
	void OnAbilityInputPressed(const FGameplayTag& InInputTag);
	void OnAbilityInputReleased(const FGameplayTag& InInputTag);
	void OnAbilitySpecInputPressed(FGameplayAbilitySpecHandle SpecHandle);
	void OnAbilitySpecInputReleased(FGameplayAbilitySpecHandle SpecHandle);

	/**
	 * Resolves one granted ability by its ability tag.
	 * Ambiguous matches intentionally fail instead of activating a random spec.
	 */
	FGameplayAbilitySpecHandle FindUniqueAbilitySpecHandleByTag(
		const FGameplayTag& AbilityTag);

	bool IsAbilitySpecInputPressed(FGameplayAbilitySpecHandle SpecHandle) const;

	/** Narrow compatibility surface required by the Gladiator cooldown-duration execution. */
	FActiveGameplayEffect* GetActiveGameplayEffectMutable(FActiveGameplayEffectHandle Handle);
	TArray<FActiveGameplayEffectHandle> GetAllActiveEffectHandles() const;
	void MarkActiveGameplayEffectDirty(FActiveGameplayEffect* ActiveGameplayEffect);
	void CheckActiveEffectDuration(FActiveGameplayEffectHandle Handle);

	UFUNCTION(BlueprintCallable, Category = "RPG|Ability", meta=(ApplyLevel="1"))
		void GrantPlayerWeaponAbilities(const TArray<FRPGPlayerAbilitySet> InDefaultWeaponAbilities, 
			const TArray<FRPGPlayerSkillSet>& InSkillAbilities,
			int32 ApplyLevel, TArray<FGameplayAbilitySpecHandle>& OutGrantedAbilitySpecHandles);

	UFUNCTION(BlueprintCallable, Category = "RPG|Ability")
		void RemovedGrantedPlayerWeaponAbilities(UPARAM(ref) TArray<FGameplayAbilitySpecHandle>& InSpecHandlesToRemove);

	UFUNCTION(BlueprintCallable, Category = "RPG|Ability")
		bool TryActivateAbilityByTag(FGameplayTag AbilityTagToActivate);

protected:
	// Preserve server-requested local presentation until the owning Pawn is ready.
	// Missing specs still use GAS's own replication queue; predicted combat is unchanged.
	virtual void ClientTryActivateAbility_Implementation(FGameplayAbilitySpecHandle AbilityToActivate) override;

	// Override implementations, not RPC declarations: GAS batching dispatches here too.
	virtual void ServerSetReplicatedEvent_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
		FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey) override;
	virtual void ServerSetReplicatedEventWithPayload_Implementation(EAbilityGenericReplicatedEvent::Type EventType,
		FGameplayAbilitySpecHandle AbilityHandle, FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey,
		FVector_NetQuantize100 VectorPayload) override;
	virtual void ServerSetReplicatedTargetData_Implementation(FGameplayAbilitySpecHandle AbilityHandle,
		FPredictionKey AbilityOriginalPredictionKey, const FGameplayAbilityTargetDataHandle& TargetData,
		FGameplayTag ApplicationTag, FPredictionKey CurrentPredictionKey) override;
	virtual void ServerSetReplicatedTargetDataCancelled_Implementation(FGameplayAbilitySpecHandle AbilityHandle,
		FPredictionKey AbilityOriginalPredictionKey, FPredictionKey CurrentPredictionKey) override;
	virtual void ServerEndAbility_Implementation(FGameplayAbilitySpecHandle AbilityToEnd,
		FGameplayAbilityActivationInfo ActivationInfo, FPredictionKey PredictionKey) override;
	virtual void ClientEndAbility_Implementation(FGameplayAbilitySpecHandle AbilityToEnd,
		FGameplayAbilityActivationInfo ActivationInfo) override;

	/** Server-authored BP default; never supplied by a client request. Includes cancellations. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Security|TargetData", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaximumTargetDataRequestsPerSecond = 64;

	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

private:
    FRPGRidingAbilityGate RidingAbilityGate;
	friend struct FRPGRpcIngressTestAccess;
	bool AdmitGenericReplicatedEvent(EAbilityGenericReplicatedEvent::Type EventType,
		FGameplayAbilitySpecHandle Handle, FPredictionKey OriginalKey);
	FRPGServerRequestBudget GenericEventRequestBudget;
	FGuid CombatMontagePlayRequest;
	friend struct FRPGLocalAbilityStartupTestAccess;
	bool IsLocalAbilityActorInfoReady() const;
	void FlushDeferredLocalOnlyActivations();
	struct FDeferredLocalOnlyActivation
	{
		TWeakObjectPtr<AActor> Owner;
		TWeakObjectPtr<AActor> Avatar;
		TWeakObjectPtr<UWorld> World;
	};
	TMap<FGameplayAbilitySpecHandle, FDeferredLocalOnlyActivation> DeferredLocalOnlyActivations;

	bool bDungeonGameplayAuthorityRevoked = false;
	bool AdmitTargetDataRequest(FGameplayAbilitySpecHandle Handle, FPredictionKey OriginalKey,
		FPredictionKey MessageKey, bool bCancellation, const FGameplayAbilityTargetDataHandle* Data);
	void ReportTargetDataAdmissionFailure(const TCHAR* Reason, double Now);
	TMap<FGameplayAbilitySpecHandleAndPredictionKey, FRPGTargetDataRequestSequence> TargetDataRequestSequences;
	FRPGTargetDataRequestWindow TargetDataRequestWindow;
	double LastTargetDataAdmissionReportAt = -1.0;
#if !UE_BUILD_SHIPPING
	double LastSkillClientEndTraceAt = -1.0;
#endif
};
