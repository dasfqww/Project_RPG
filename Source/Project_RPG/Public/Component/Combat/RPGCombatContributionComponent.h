#pragma once

#include "Components/ActorComponent.h"
#include "Combat/RPGCombatContributionTypes.h"
#include "RPGCombatContributionComponent.generated.h"

class UGameplayAbility;
class URPGGameplayAbility_SkillContainer;
struct FRPGApprovedHitContext;
class FDataValidationContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGOnCombatContributionChanged, FRPGCombatContributionState, State);

/** Opt-in, target-owned mechanic meter. Only the native approved skill-hit path may add units. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGCombatContributionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGCombatContributionComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	UFUNCTION(BlueprintPure, Category="Combat|Contribution")
	FRPGCombatContributionState GetContributionState() const { return State; }
	const FRPGCombatContributionReceiverProfile& GetReceiverProfile() const { return ReceiverProfile; }
	UPROPERTY(BlueprintAssignable, Category="Combat|Contribution")
	FRPGOnCombatContributionChanged OnContributionChanged;
	/** Trusted server/content initialization; no Blueprint or RPC setter. Resets this receiver only. */
	bool ConfigureForNewAttempt(const FRPGCombatContributionReceiverProfile& Profile);
	void ResetForAvatarReuse();
protected:
	UPROPERTY(EditDefaultsOnly, Category="Combat|Contribution")
	FRPGCombatContributionReceiverProfile ReceiverProfile;
private:
	friend class URPGGameplayAbility_SkillContainer;
	void AcceptApprovedSkillHit(URPGGameplayAbility_SkillContainer& Ability, uint64 Activation, uint32 Generation,
		AActor* Source, const FRPGApprovedHitContext& Lifetime, const FRPGCombatContributionAmounts& Amounts);
	void Publish();
	UFUNCTION() void OnRep_State();
	UPROPERTY(ReplicatedUsing=OnRep_State) FRPGCombatContributionState State;
	TMap<TWeakObjectPtr<UGameplayAbility>, uint64> SeenActivations;
	bool bApplying = false, bResetting = false, bEnding = false;
};
