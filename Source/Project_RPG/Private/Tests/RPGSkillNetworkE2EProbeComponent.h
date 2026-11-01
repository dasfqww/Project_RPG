#pragma once

#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "RPGSkillNetworkE2EProbeComponent.generated.h"

class URPGAbilitySystemComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
struct FAbilityEndedData;
struct FGameplayAbilityTargetDataHandle;
class FRPGSkillTripodNetworkProbe;
class FRPGSkillDamageNetworkProbe;
class FRPGSkillFinalCapNetworkProbe;
class FRPGNPCDeathNetworkProbe;
class UAbilitySystemComponent;
class URPGGameplayAbility_SkillContainer;
struct FRPGHitQueryResult;

/**
 * Development-only network probe for the authored Enhanced Input charge action.
 * The fixture grants ChargeSlash and Rage; normal loadout/tripod acquisition and
 * physical key hardware are outside this probe. Disabled without -RPGSkillNetE2E.
 * -RPGSkillProgressionE2E additionally exercises UI view-model level commands,
 * owner replication, the activation snapshot and full SP refund (not tripods).
 */
UCLASS(Transient, NotBlueprintable)
class URPGSkillNetworkE2EProbeComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGSkillNetworkE2EProbeComponent();
	/** Native dev observation bridge for the real authored ability/ASC send and receive paths. */
	static void ObserveTargetData(URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle,
		FPredictionKey Original, const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag,
		FPredictionKey Message, bool bSending, bool bAdmitted);
	static void ObserveFinalCapExecution(const UAbilitySystemComponent& Source, const UAbilitySystemComponent& Target,
		double Calculated, double Cap, bool bBound, bool bApproved);
	static void ObserveFinalCapApplication(URPGGameplayAbility_SkillContainer& Skill,
		const TArray<FRPGHitQueryResult>& Hits, int32 AppliedCount, int32 BudgetUsed);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	enum class EClientPhase : uint8
	{
		WaitingForAbility,
		WaitingForProgression,
		WaitingForActivation,
		HoldingInput,
		WaitingForReleaseEvent,
		WaitingForEnd,
		WaitingForReset,
		Finished
	};

	FGameplayAbilitySpec* FindChargeSkillSpec(
		URPGAbilitySystemComponent& AbilitySystem) const;
	void TickClient(URPGAbilitySystemComponent& AbilitySystem, double NowSeconds);
	void TickServer(URPGAbilitySystemComponent& AbilitySystem);
	bool PrepareClientInput();
	bool RequestClientProgression(int32 TargetLevel);
	bool HasReplicatedProgression(int32 ExpectedLevel) const;
	void ObserveAbilityEnd(URPGAbilitySystemComponent& AbilitySystem);
	void HandleAbilityEnded(const FAbilityEndedData& EndData);
	void ObserveReleaseSection(URPGAbilitySystemComponent& AbilitySystem,
		const FGameplayAbilitySpec& Spec);
	void FailProbe(const TCHAR* Reason);
	void FinishProbe();

	double DeadlineSeconds = 0.0;
	double ReleaseInputAtSeconds = 0.0;
	double NextActivationAttemptSeconds = 0.0;
	FGameplayAbilitySpecHandle SkillHandle;
	FDelegateHandle AbilityEndedHandle;
	TWeakObjectPtr<URPGAbilitySystemComponent> ObservedAbilitySystem;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	TWeakObjectPtr<const UInputAction> SkillInputAction;
	EClientPhase ClientPhase = EClientPhase::WaitingForAbility;
	bool bServerObservedActive = false;
	bool bObservedAbilityEnd = false;
	bool bObservedCancelledEnd = false;
	bool bObservedReleaseSection = false;
	bool bServerProvisionedAbility = false;
	bool bServerProvisionedRage = false;
	bool bLoggedMissingClientSpec = false;
	bool bFinished = false;
	// Opt-in -RPGSkillTripodE2E: transient choices, rejection and SP rollback.
	TSharedPtr<FRPGSkillTripodNetworkProbe> TripodProbe;
	TSharedPtr<FRPGSkillDamageNetworkProbe> DamageProbe;
	TSharedPtr<FRPGSkillFinalCapNetworkProbe> FinalCapProbe;
	TSharedPtr<FRPGNPCDeathNetworkProbe> NPCDeathProbe;
};
