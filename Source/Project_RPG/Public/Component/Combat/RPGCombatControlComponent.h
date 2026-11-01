#pragma once

#include "Components/ActorComponent.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGAirborneTypes.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGBurnTypes.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "Combat/RPGDamageTakenIncreaseTypes.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Combat/RPGCombatImmunityTypes.h"
#include "Combat/RPGMontageImmunityTypes.h"
#include "Combat/RPGStaggerTypes.h"
#include "GameplayEffectTypes.h"
#include "RPGCombatControlComponent.generated.h"

class UAbilitySystemComponent;
class AController;
class UNiagaraSystem;
class UNiagaraComponent;
class UAnimInstance;
class UAnimMontage;
class URPGHitReactionProfile;
class URPGAirbornePresentationProfile;
class UWidgetComponent;
class UGameplayAbility;
class URPGGameplayAbility;
class UAnimMontage;
struct FRPGMontageImmunityRuntime;
struct FOnAttributeChangeData;
struct FGameplayEffectRemovalInfo;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnStunStateChanged, const FRPGStunPresentationState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnKnockbackStateChanged, const FRPGKnockbackState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnAirborneStateChanged, const FRPGAirborneState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnSlowStateChanged, const FRPGSlowPresentationState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnBurnStateChanged, const FRPGBurnPresentationState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnStaggerStateChanged, const FRPGStaggerState&, State);

/** Shared GE-backed Player/NPC control. No private duration timer or movement-mode restoration. */
UCLASS(ClassGroup=(RPG), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGCombatControlComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGCombatControlComponent();
	void InitializeWithAbilitySystem(UAbilitySystemComponent& ASC);
	/** Native damage approvers only. No BP/RPC endpoint that fabricates an approved hit. */
	FActiveGameplayEffectHandle ApplyApprovedDamageStun(AActor* Source, const FHitResult& ServerHit,
		const FGuid& ApprovedHitId, float Duration);
	/** Removes exactly this receiver's handle, preserving overlapping sources. */
	bool RemoveOwnedStun(FActiveGameplayEffectHandle Handle);
	FActiveGameplayEffectHandle ApplyApprovedDamageStagger(AActor* Source, const FHitResult& ServerHit,
		const FGuid& ApprovedHitId, const FRPGStaggerSettings& Settings, const FVector& AwayDirection);
	bool RemoveOwnedStagger(FActiveGameplayEffectHandle Handle);
	UFUNCTION(BlueprintPure, Category="Combat|Control")
	FRPGStaggerState GetStaggerState() const { return StaggerState; }
	UPROPERTY(BlueprintAssignable, Category="Combat|Control")
	FRPGOnStaggerStateChanged OnStaggerStateChanged;
	FActiveGameplayEffectHandle ApplyApprovedDamageDefenseReduction(AActor* Source, const FHitResult& ServerHit,
		const FGuid& ApprovedHitId, const FRPGDefenseReductionSettings& Settings);
	bool RemoveOwnedDefenseReduction(FActiveGameplayEffectHandle Handle);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Combat|Status")
	void CleanseDefenseReductions();
	void SetDefaultDefenseReductionImmunity(bool bImmune) { bDefenseReductionImmune = bImmune; }
	FActiveGameplayEffectHandle ApplyApprovedDamageTakenIncrease(AActor* Source, const FHitResult& ServerHit,
		const FGuid& ApprovedHitId, const FRPGDamageTakenIncreaseSettings& Settings);
	bool RemoveOwnedDamageTakenIncrease(FActiveGameplayEffectHandle Handle);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Combat|Status")
	void CleanseDamageTakenIncreases();
	void SetDefaultDamageTakenIncreaseImmunity(bool bImmune) { bDamageTakenIncreaseImmune = bImmune; }
	/** Derived from this server receiver's active GEs; no cached attribute or client authority. */
	double GetDamageTakenMultiplier() const;
	void ResetForAvatarReuse();
	UFUNCTION(BlueprintPure, Category="Combat|Immunity")
	bool IsImmuneTo(ERPGCombatEffectKind Kind) const;
	void SetDefaultHitReactionImmunity(bool bImmune) { DefaultImmunities.Flags = bImmune ? DefaultImmunities.Flags | 1 : DefaultImmunities.Flags & ~1; }
	/** Native active-ability path only. Token is reserved before GE callbacks can reenter EndAbility. */
	bool BeginAbilityImmunity(UGameplayAbility& Ability, FGuid Token, const FRPGCombatImmunitySettings& Settings);
	void EndAbilityImmunity(const FGuid& Token);
	/** Native ASC/notify hooks only. Clients cannot grant or time their own protection. */
	void TrackMontageImmunity(UAbilitySystemComponent& ASC, URPGGameplayAbility& Ability, FGuid Execution,
		UAnimMontage& Montage, TArray<FRPGMontageImmunityWindow> Windows);
	void RefreshMontageImmunity();
	void StopMontageImmunity();
	void EndMontageImmunityForAbility(UGameplayAbility& Ability, FGuid Execution);
	bool HasMontageImmunityTracking() const { return MontageImmunityRuntime.IsValid(); }
	void SetDefaultStunImmunity(bool bImmune) { bStunImmune = bImmune; }
	void SetDefaultKnockbackImmunity(bool bImmune) { bKnockbackImmune = bImmune; }
	void SetDefaultAirborneImmunity(bool bImmune) { bAirborneImmune = bImmune; }
	void SetDefaultSlowImmunity(bool bImmune) { bSlowImmune = bImmune; }
	FActiveGameplayEffectHandle ApplyApprovedDamageSlow(AActor* Source, const FHitResult& ServerHit,
		const FGuid& ApprovedHitId, const FRPGSlowSettings& Settings);
	bool RemoveOwnedSlow(FActiveGameplayEffectHandle Handle);
	FActiveGameplayEffectHandle ApplyApprovedDamageBurn(AActor* Source, const FHitResult& ServerHit,
		const FGuid& ApprovedHitId, const FRPGBurnSettings& Settings);
	bool RemoveOwnedBurn(FActiveGameplayEffectHandle Handle);
	/** Receiver-owned burn statuses only; never removes equipment, buffs or another system's GE. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Combat|Status")
	void CleanseBurns();
	UFUNCTION(BlueprintPure, Category="Combat|Status")
	FRPGBurnPresentationState GetBurnPresentationState() const { return BurnState; }
	UPROPERTY(BlueprintAssignable, Category="Combat|Status")
	FRPGOnBurnStateChanged OnBurnStateChanged;
	UFUNCTION(BlueprintPure, Category="Combat|Status")
	FRPGSlowPresentationState GetSlowPresentationState() const { return SlowState; }
	UPROPERTY(BlueprintAssignable, Category="Combat|Status")
	FRPGOnSlowStateChanged OnSlowStateChanged;
	/** Native approved-hit path only; direction was captured before damage callbacks. */
	bool ApplyApprovedDamageKnockback(AActor* Source, const FHitResult& ServerHit, const FGuid& ApprovedHitId,
		const FRPGKnockbackSettings& Settings, const FVector& AwayDirection);
	bool CancelKnockback(uint32 ExecutionId);
	bool HasKnockbackMotion() const;
	bool HasAirborneMotion() const;
	bool CancelAirborne(uint32 ExecutionId);
	void HandleAirborneImpact(const FHitResult& Hit);
	UFUNCTION(BlueprintPure, Category="Combat|Control")
	FRPGAirborneState GetAirborneState() const { return AirborneState; }
	UPROPERTY(BlueprintAssignable, Category="Combat|Control")
	FRPGOnAirborneStateChanged OnAirborneStateChanged;
	uint32 GetLifetimeEpoch() const { return LifetimeEpoch; }
	void HandleKnockbackImpact(const FHitResult& Hit);
	UFUNCTION(BlueprintPure, Category="Combat|Control")
	FRPGKnockbackState GetKnockbackState() const { return KnockbackState; }
	UPROPERTY(BlueprintAssignable, Category="Combat|Control")
	FRPGOnKnockbackStateChanged OnKnockbackStateChanged;

	UFUNCTION(BlueprintPure, Category="Combat|Control")
	FRPGStunPresentationState GetStunPresentationState() const { return StunState; }
	UPROPERTY(BlueprintAssignable, Category="Combat|Control")
	FRPGOnStunStateChanged OnStunStateChanged;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Combat|Immunity")
	FRPGCombatImmunitySettings DefaultImmunities;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Control")
	bool bStunImmune = false;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Control")
	bool bKnockbackImmune = false;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Control")
	bool bAirborneImmune = false;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Status")
	bool bSlowImmune = false;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Status")
	bool bDefenseReductionImmune = false;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Status")
	bool bDamageTakenIncreaseImmune = false;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Status|Presentation")
	bool bShowSlowStatus = true;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Control|Presentation")
	TObjectPtr<URPGAirbornePresentationProfile> AirbornePresentationProfile;
	/** Optional override; otherwise reuse the actor's safe hit-reaction profile. */
	UPROPERTY(EditDefaultsOnly, Category="Combat|Control|Presentation")
	TObjectPtr<URPGHitReactionProfile> KnockbackPresentationProfile;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Control|Presentation")
	TSoftObjectPtr<UNiagaraSystem> StunVisual;
private:
	TMap<FActiveGameplayEffectHandle, int32> OwnedDamageTakenIncreases;
	TArray<FGuid> RecentDamageTakenIncreaseHits;
	bool bApplyingDamageTakenIncrease = false, bClearingDamageTakenIncreases = false;
	uint32 DamageTakenIncreaseEpoch = 0;
	void ClearOwnedDamageTakenIncreases();
	void RefreshDamageTakenIncreases();
	void HandleDamageTakenIncreaseRemoved(const FGameplayEffectRemovalInfo& Info);
	TMap<FActiveGameplayEffectHandle, int32> OwnedDefenseReductions;
	TArray<FGuid> RecentDefenseReductionHits;
	bool bApplyingDefenseReduction = false, bClearingDefenseReductions = false;
	uint32 DefenseReductionEpoch = 0;
	void ClearOwnedDefenseReductions();
	void RefreshDefenseReductions();
	void HandleDefenseReductionRemoved(const FGameplayEffectRemovalInfo& Info);
	UPROPERTY(ReplicatedUsing=OnRep_StaggerState) FRPGStaggerState StaggerState;
	TSet<FActiveGameplayEffectHandle> OwnedStaggers;
	TArray<FGuid> RecentStaggerHits;
	FDelegateHandle StaggerTagHandle;
	bool bApplyingStagger = false, bClearingStaggers = false;
	uint32 StaggerSequence = 0, StaggerPoseExecution = 0, StaggerPublishedSequence = 0;
	TWeakObjectPtr<UAnimInstance> StaggerAnimInstance;
	TWeakObjectPtr<UAnimMontage> StaggerMontage;
	int32 StaggerMontageInstanceId = INDEX_NONE;
	void ClearOwnedStaggers();
	void RefreshStaggerState();
	void UpdateStaggerPose();
	void StopStaggerPose();
	void HandleStaggerRemoved(const FGameplayEffectRemovalInfo& Info);
	void HandleStaggerInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited);
	UFUNCTION() void OnRep_StaggerState();
	struct FBurnLease
	{
		TWeakObjectPtr<AActor> Source;
		TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
		TWeakObjectPtr<AController> SourceController;
		FRPGApprovedHitContext Lifetime;
		FRPGBurnSettings Settings;
		double NextTick = 0.;
		int32 EncounterId = 0;
		int32 SourceEncounterId = 0;
	};
	TMap<FActiveGameplayEffectHandle, FBurnLease> OwnedBurns;
	TArray<FGuid> RecentBurnHits;
	bool bApplyingBurn = false, bClearingBurns = false, bTickingBurn = false;
	uint32 BurnEpoch = 0;
	UPROPERTY(ReplicatedUsing=OnRep_BurnState) FRPGBurnPresentationState BurnState;
	UPROPERTY(Transient) TObjectPtr<UWidgetComponent> BurnStatusWidget;
	UPROPERTY(EditDefaultsOnly, Category="Combat|Status|Presentation") bool bShowBurnStatus = true;
	void ClearOwnedBurns();
	void TickBurns();
	void RefreshBurnState();
	void UpdateBurnVisual();
	void HandleBurnRemoved(const FGameplayEffectRemovalInfo& Info);
	void HandleBurnInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited);
	UFUNCTION() void OnRep_BurnState();
	struct FAbilityImmunityLease
	{
		TWeakObjectPtr<UGameplayAbility> Ability;
		FActiveGameplayEffectHandle Effect;
		FGameplayEffectContextHandle Context;
	};
	TSharedPtr<FRPGMontageImmunityRuntime> MontageImmunityRuntime;
	bool bRefreshingMontageImmunity = false;
	TMap<FGuid, FAbilityImmunityLease> AbilityImmunities;
	bool bClearingAbilityImmunities = false;
	void ClearAbilityImmunities();
	void RefreshAbilityImmunities();
	UPROPERTY(ReplicatedUsing=OnRep_SlowState)
	FRPGSlowPresentationState SlowState;
	TSet<FActiveGameplayEffectHandle> OwnedSlows;
	TArray<FGuid> RecentSlowHits;
	FDelegateHandle SlowTagHandle;
	bool bApplyingSlow = false;
	UPROPERTY(Transient) TObjectPtr<UWidgetComponent> SlowStatusWidget;
	void UpdateSlowVisual();
	void ClearOwnedSlows();
	void RefreshSlowState();
	void HandleSlowRemoved(const FGameplayEffectRemovalInfo& Info);
	void HandleSlowInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited);
	UFUNCTION() void OnRep_SlowState();
	UPROPERTY(ReplicatedUsing=OnRep_StunState)
	FRPGStunPresentationState StunState;
	UPROPERTY(ReplicatedUsing=OnRep_KnockbackState)
	FRPGKnockbackState KnockbackState;
	UPROPERTY(ReplicatedUsing=OnRep_AirborneState)
	FRPGAirborneState AirborneState;
	FActiveGameplayEffectHandle OwnedAirborneSequence, OwnedAirbornePhase;
	FDelegateHandle AirborneTagHandle;
	FGuid AirborneMovementAuthorization;
	TArray<FGuid> RecentAirborneHits;
	FName AirborneMotionName;
	FVector AirborneLastLocation = FVector::ZeroVector;
	// Airborne and ground knockback are mutually exclusive and share this carry baseline.
	FVector DisplacementLastBaseMovementTotal = FVector::ZeroVector;
	uint32 AirborneSequence = 0, AirbornePoseRevision = 0;
	bool bApplyingAirborne = false, bEndingAirborne = false, bChangingAirbornePhase = false, bAirborneHasMoved = false;
	TWeakObjectPtr<UAnimInstance> AirborneAnimInstance;
	TWeakObjectPtr<UAnimMontage> AirborneMontage;
	int32 AirborneMontageInstanceId = INDEX_NONE;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> StunVisualInstance;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	TSet<FActiveGameplayEffectHandle> OwnedStuns;
	TArray<FGuid> RecentApprovedHits;
	FDelegateHandle StunTagHandle, DeathTagHandle, HealthHandle;
	FDelegateHandle KnockbackTagHandle;
	FActiveGameplayEffectHandle OwnedKnockback;
	FGuid KnockbackMovementAuthorization;
	TArray<FGuid> RecentKnockbackHits;
	FName KnockbackMotionName;
	FVector KnockbackLastLocation = FVector::ZeroVector;
	uint16 KnockbackMotionId = 0;
	uint32 KnockbackSequence = 0;
	uint32 KnockbackPoseExecution = 0;
	bool bApplyingKnockback = false, bEndingKnockback = false;
	TWeakObjectPtr<UAnimInstance> KnockbackAnimInstance;
	TWeakObjectPtr<UAnimMontage> KnockbackMontage;
	int32 KnockbackMontageInstanceId = INDEX_NONE;
	uint32 BoundRecoveryRevision = 0;
	uint32 LifetimeEpoch = 0;
	bool bActiveControl = false;
	bool bRefreshing = false;
	bool bApplying = false;
	bool bEnding = false;
	bool IsLivingAvatar(const AActor* Actor) const;
	uint32 RecoveryRevision() const;
	bool HasUncancellableCombatAbility() const;
	void CancelCombatAbilities();
	void ClearOwnedStuns();
	void Unbind();
	void RefreshControl();
	void UpdateVisual();
	void HandleTagChanged(FGameplayTag Tag, int32 Count);
	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleStunRemoved(const FGameplayEffectRemovalInfo& Info);
	void HandleStunInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited);
	void TickKnockback();
	void EndKnockback(ERPGKnockbackEndReason Reason);
	void ApplyKnockbackMotion();
	void StopKnockbackMotion();
	void UpdateKnockbackPose();
	void StopKnockbackPose();
	void HandleKnockbackRemoved(const FGameplayEffectRemovalInfo& Info);
	void HandleKnockbackInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited);
	UFUNCTION() void OnKnockbackMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	UFUNCTION() void OnRep_KnockbackState();
	UFUNCTION() void OnRep_StunState();
	bool ApplyApprovedDamageAirborne(AActor* Source, const FHitResult& Hit, const FGuid& Receipt,
		const FRPGKnockbackSettings& Settings, const FVector& AwayDirection);
	bool ChangeAirbornePhase(ERPGAirbornePhase Phase);
	void TickAirborne();
	void EndAirborne(ERPGAirborneEndReason Reason);
	void ApplyAirborneMotion();
	void StopAirborneMotion();
	void ReleaseAirborneAuthorization();
	void UpdateAirbornePose();
	void StopAirbornePose();
	void PublishAirborneState();
	void HandleAirborneRemoved(const FGameplayEffectRemovalInfo& Info);
	void HandleAirborneInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited);
	UFUNCTION() void OnAirborneLanded(const FHitResult& Hit);
	UFUNCTION() void OnAirborneMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	UFUNCTION() void OnRep_AirborneState();
	friend struct FRPGCombatControlTestAccess;
	friend struct FRPGKnockbackTestAccess;
	friend struct FRPGAirborneTestAccess;
	friend struct FRPGSlowTestAccess;
	friend struct FRPGImmunityTestAccess;
	friend struct FRPGBurnTestAccess;
	friend struct FRPGStaggerTestAccess;
	friend class URPGBossMapE2EProbeComponent;
};
