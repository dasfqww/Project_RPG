#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Pawn.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Interface/PawnUIInterface.h"
#include "RPGSkillDamageNetworkProbe.generated.h"

class URPGAbilitySystemComponent;
class URPGAttributeSet;
class UPawnUIComponent;
class URPGCombatContributionComponent;
class APlayerController;
class URPGGameplayAbility_SkillContainer;

/** Opt-in replicated target only. No damage RPC, synthetic effect or BP asset mutation. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class ARPGSkillDamageNetworkTarget final : public APawn,
	public IAbilitySystemInterface, public IPawnUIInterface
{
	GENERATED_BODY()
public:
	ARPGSkillDamageNetworkTarget();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual UPawnUIComponent* GetPawnUIComponent() const override;
	virtual UPlayerUIComponent* GetPlayerUIComponent() const override { return nullptr; }
	virtual UNPCUIComponent* GetNPCUIComponent() const override { return nullptr; }
	URPGAbilitySystemComponent* ASC() const { return TargetASC; }
	URPGAttributeSet* Attributes() const { return TargetAttributes; }
	URPGCombatContributionComponent* Contributions() const { return TargetContributions; }
	static constexpr float BaselineHealth = 1000000.0f;
	UPROPERTY(Replicated) int32 TestIndex = INDEX_NONE;
	UPROPERTY(Replicated) float ExpectedHealth = BaselineHealth;
	UPROPERTY(Replicated) bool bSuiteFinished = false;
	UPROPERTY(Replicated) bool bAbuseRequestsVerified = false;
	// Multi-peer fixture coordination only; never authorizes a hit or applies damage.
	UPROPERTY(Replicated) int32 SourcePeer = INDEX_NONE;
	UPROPERTY(Replicated) uint64 SourceASCNetId = 0;
	UPROPERTY(Replicated) FVector FixtureSourceLocation = FVector::ZeroVector;
	UPROPERTY(Replicated) bool bInputReady = false;
	UPROPERTY(Replicated) bool bPeerPrimed = false;
	UPROPERTY(Replicated) int32 AppliedEffects = 0;
	UPROPERTY(Replicated) int32 ObservedEvents = 0;
private:
	UPROPERTY() TObjectPtr<URPGAbilitySystemComponent> TargetASC;
	UPROPERTY() TObjectPtr<URPGAttributeSet> TargetAttributes;
	UPROPERTY() TObjectPtr<UPawnUIComponent> TargetUI;
	UPROPERTY() TObjectPtr<URPGCombatContributionComponent> TargetContributions;
};

/** Owned coordination actor only. AI-possessed target Pawns are not RPC owners. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class ARPGSkillMultiPeerCoordinator final : public AActor
{
	GENERATED_BODY()
public:
	ARPGSkillMultiPeerCoordinator();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(Replicated) int32 SourcePeer = INDEX_NONE;
	UPROPERTY(Replicated) uint64 SourceASCNetId = 0;
	UFUNCTION(Server, Reliable) void ServerFixtureReady();
private:
	bool bFixtureReadyReceived = false;
};

/** Observes real authored ChargeSlash input/montage/event/GE/HP replication.
 * -RPGSkillTargetDataAbuseE2E additionally sends malicious RPCs on the real player ASC.
 * -RPGSkillMultiPeerE2E adds two synchronized, spatially separated player sources.
 * Never invokes a damage helper or synthesizes a damage event. */
class FRPGSkillDamageNetworkProbe
{
public:
	~FRPGSkillDamageNetworkProbe();
	void Tick(APlayerController& Controller, URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle);
	bool ReadyForInput() const { return bClientReady && (!bMultiPeer || bMultiInputReady); }
	bool NeedsInputPriming() const { return bMultiPeer && AbuseOriginal.IsValidKey() && !bAbusePrimed && !bAbuseQueued; }
	static FString LogContext(const AActor* Actor);
	bool Passed() const { return bPassed; }
	const FString& Failure() const { return FailureReason; }
	void ObserveSentTargetData(URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle,
		FPredictionKey Original, const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag, FPredictionKey Message);
	void ObserveReceivedTargetData(URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle,
		FPredictionKey Original, const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag,
		FPredictionKey Message, bool bAdmitted);
	bool HoldForAbuseVerification(URPGAbilitySystemComponent& ASC);
private:
	void Fail(const TCHAR* Reason);
	void TickServer(APlayerController& Controller, URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle);
	void TickClient(APlayerController& Controller);
	TWeakObjectPtr<ARPGSkillDamageNetworkTarget> Targets[4];
	TWeakObjectPtr<ARPGSkillDamageNetworkTarget> ForeignTargets[4];
	FDelegateHandle ForeignHealthHandles[4];
	bool bForeignHealthNotified[4] = {};
	TWeakObjectPtr<APlayerController> SourceController;
	bool bMultiPeer = false;
	bool bMultiInputReady = false;
	bool bMultiReadySent = false;
	bool bMultiAllVerified = false;
	FDelegateHandle EffectHandles[4];
	FDelegateHandle HealthHandles[4];
	TWeakObjectPtr<URPGAbilitySystemComponent> SourceASC;
	TWeakObjectPtr<URPGGameplayAbility_SkillContainer> SourceSkill;
	uint8 SourceTeamId = 255;
	FDelegateHandle EventHandle;
	int32 EffectCounts[4] = {};
	bool bHealthNotified[4] = {};
	bool bPrepared = false;
	bool bClientReady = false;
	bool bObservedActive = false;
	bool bPassed = false;
	int32 EventCount = 0;
	float ExpectedDamage = 0.0f;
	float ExpectedSetByCaller = 0.0f;
	double EndedAt = -1.0;
	FGameplayAbilitySpecHandle AbuseHandle;
	FPredictionKey AbuseOriginal;
	FPredictionKey AbuseInitialMessage;
	FPredictionKey AbuseLastFreshMessage;
	FGameplayAbilityTargetDataHandle AbuseInitialData;
	int32 AbuseReceipts = 0;
	bool bAbuseQueued = false;
	bool bAbusePrimed = false;
	bool bAbuseReleaseReady = false;
	bool bAbuseRecoveryObserved = false;
	FString FailureReason;
};
