#pragma once

#include "AbilitySystemInterface.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "Interface/PawnUIInterface.h"
#include "GameplayEffectTypes.h"
#include "RPGBossTestFixture.generated.h"

class UAbilitySystemComponent;
class URPGAttributeSet;
class URPGBossAIComponent;
class URPGBossDefinition;
class ARPGBossStartupTestCharacter;
class URPGBossAbilityTestProbe;
class URPGBossAttackTestProbe;
class URPGBossTelegraphedStrikeAbility;
class ARPGBossPersistentHazard;

UCLASS(Transient, NotBlueprintable)
class ARPGBossThreatProbeController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()
public:
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(0); }
};

/** Isolated GAS/state fixture; no production NPC assets or combat are required. */
UCLASS(Transient, NotBlueprintable)
class ARPGBossTestPawn : public APawn, public IAbilitySystemInterface, public IPawnUIInterface
{
	GENERATED_BODY()
public:
	ARPGBossTestPawn();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual UPawnUIComponent* GetPawnUIComponent() const override { return PawnUI; }
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	static URPGBossDefinition* MakeDefinition(UObject* Outer);
	void SetTestHealth(float Health);
	void SetTestRole(ENetRole NewRole) { SetRole(NewRole); }

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;
	UPROPERTY()
	TObjectPtr<URPGAttributeSet> Attributes;
	UPROPERTY()
	TObjectPtr<URPGBossAIComponent> Boss;
	UPROPERTY()
	TObjectPtr<UPawnUIComponent> PawnUI;

private:
	bool bSawHazardState = false;
	bool bSawHazardPulse = false;
	bool bSawHazardTargetDamage = false;
	bool bSawHazardRemoved = false;
	TWeakObjectPtr<ARPGBossPersistentHazard> ObservedHazard;
	bool bSawEnrageClock = false;
	bool bSawPhaseTwo = false;
	bool bSawReset = false;
	bool bSawRestart = false;
	bool bSawDead = false;
	bool bSawBossAttackDamage = false;
	bool bSawTimedStrikeDamage = false;
};

/** Only drives a scenario with -RPGBossNetworkTest in a non-shipping executable. */
UCLASS(Transient, NotBlueprintable)
class ARPGBossNetworkTestGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARPGBossNetworkTestGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
private:
	bool BeginNetworkSummon();
	void TickNetworkSummon();
	void RecordSummonLateJoin();
	FString SummonGateBase;
	FString DeferredSummonAddress;
	UPROPERTY() TObjectPtr<ARPGBossStartupTestCharacter> SummonBoss;
	UPROPERTY() TObjectPtr<ARPGBossTestPawn> SummonTarget;
	TArray<TWeakObjectPtr<class ARPGNonPlayerCharacter>> SummonMembers;
	TArray<TWeakObjectPtr<AController>> SummonControllers;
	FGameplayAbilitySpecHandle SummonCastHandle;
	int32 SummonStage = 0;
	bool bSummonLateJoin = false;
	bool bSummonDeferredReady = false;
	bool bSummonDeferredTravel = false;
	double SummonResetTime = 0.0;
	double SummonExpiry = 0.0;
	bool BeginDeferredDashJoin();
	void TickNetworkDash();
	void RecordDashLateJoin();
	FString DashGateBase;
	FString DeferredDashAddress;
	bool bDeferredDashReady = false;
	bool bDeferredDashTravel = false;
	UPROPERTY() TObjectPtr<ARPGBossStartupTestCharacter> DashBoss;
	UPROPERTY() TObjectPtr<ARPGBossTestPawn> DashFirst;
	UPROPERTY() TObjectPtr<ARPGBossTestPawn> DashSecond;
	FGameplayAbilitySpecHandle DashCastHandle;
	int32 DashStage = 0;
	bool bDashLateJoin = false;
	bool bDashMoved = false;
	bool bDashDamaged = false;
	double DashResetTime = 0.0;
	FVector DashStoppedLocation = FVector::ZeroVector;
	bool StartNetworkHazard();
	UPROPERTY() TObjectPtr<ARPGBossTestPawn> HazardSource;
	UPROPERTY() TObjectPtr<ARPGBossTestPawn> HazardFirst;
	UPROPERTY() TObjectPtr<ARPGBossTestPawn> HazardSecond;
	TWeakObjectPtr<ARPGBossPersistentHazard> NetworkHazard;
	FGameplayAbilitySpecHandle HazardCastHandle;
	bool bReportedHazardStarted = false;
	float HazardFirstResetHealth = 0.f;
	float HazardSecondResetHealth = 0.f;
	UPROPERTY()
	TObjectPtr<ARPGBossTestPawn> BossPawn;
	UPROPERTY()
	TObjectPtr<ARPGBossStartupTestCharacter> StartupBoss;
	UPROPERTY()
	TObjectPtr<URPGBossAbilityTestProbe> AbilityProbe;
	UPROPERTY()
	TObjectPtr<URPGBossAttackTestProbe> AttackProbe;
	UPROPERTY()
	TObjectPtr<URPGBossAbilityTestProbe> TimedStrikeProbe;
	UPROPERTY()
	TObjectPtr<URPGBossTelegraphedStrikeAbility> TimedStrikeAbility;
	UPROPERTY()
	FGameplayEffectSpecHandle PendingDamageSpec;
	TWeakObjectPtr<APawn> DamageSource;
	int32 DamageEncounterId = 0;
	int32 TelegraphSequence = 0;
	double LateJoinReadyTime = -1.0;
	bool bReportedAbilityStart = false;
	bool bReportedTimedStrikeWindup = false;
	bool bMovedTimedStrikeTarget = false;
	TArray<TWeakObjectPtr<APlayerController>> ProbePlayers;
	TWeakObjectPtr<APlayerController> ThreatVictim;
	TWeakObjectPtr<APawn> ThreatFallback;
	bool bThreatVictimLeft = false;
	int32 Stage = 0;
	double StageTime = 0.0;
};
