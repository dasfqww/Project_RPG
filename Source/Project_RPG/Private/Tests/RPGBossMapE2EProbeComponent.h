#pragma once

#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayAbilitySpec.h"
#include "RPGBossMapE2EProbeComponent.generated.h"

class APlayerController;
class APawn;
class ARPGBossCharacter;
class UUserWidget;
class UAbilitySystemComponent;
struct FGameplayEffectSpec;
struct FAbilityEndedData;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
struct FRPGMontageImmunityNetworkFixture;

/** Explicit development-only scenario on the authored bossmap/classes, not a replacement GameMode. */
UCLASS(Transient, NotBlueprintable)
class URPGBossMapE2EProbeComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGBossMapE2EProbeComponent();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
private:
	UFUNCTION(Client, Reliable) void ClientObserveKnockback(APawn* Player, APawn* NPC, int32 Step, FVector ExpectedLocation);
	UFUNCTION(Server, Reliable) void ServerConfirmKnockback(int32 Step);
	void TickKnockbackServer();
	void TickKnockbackClient();
	void TickAirborneServer();
	void TickAirborneClient();
	void TickMontageImmunityServer();
	void TickMontageImmunityClient();
	void CleanupMontageImmunityFixture();
	TSharedPtr<FRPGMontageImmunityNetworkFixture> MontageImmunityFixture;
	void TickStaggerServer();
	void TickStaggerClient();
	bool ApplyStaggerProbeDamage(bool bPlayerSource, float Duration);
	FActiveGameplayEffectHandle StaggerForeignEffect;
	void TickBurnServer();
	void TickBurnClient();
	bool ApplyBurnProbeDamage(bool bPlayerSource, int32 Damage, float Duration);
	float BurnHealthBaseline = 0.f, BurnClientHealth = -1.f;
	int32 BurnClientStep = -1;
	void TickSlowServer();
	void TickSlowClient();
	void TickImmunityServer();
	void TickImmunityClient();
	bool ApplyImmunityProbeDamage(bool bPlayerSource, bool bDisplacement, bool bAirborne, bool bStun, bool bSlow);
	FGameplayAbilitySpecHandle ImmunityPlayerA, ImmunityPlayerB, ImmunityNPC;
	FActiveGameplayEffectHandle ImmunityForeignEffect;
	bool ApplySlowProbeDamage(bool bPlayerSource, float Percent, float Duration);
	bool ApplyAirborneProbeDamage(bool bPlayerSource);
	bool ApplyKnockbackProbeDamage(bool bPlayerSource, float Distance, float Duration, float StunDuration = 0.f);
	void SendKnockbackProbeStep(int32 Step, APawn* Subject = nullptr);
	bool bKnockbackScenario = false;
	int32 KnockbackStage = -1, KnockbackExpected = -1, KnockbackConfirmed = -1, KnockbackInputStep = -1;
	int32 KnockbackViolationBaseline = 0;
	double KnockbackInputAt = 0.0;
	bool bKnockbackAttemptedInput = false;
	FVector KnockbackExpectedLocation = FVector::ZeroVector;
	FVector KnockbackProbeOrigin = FVector::ZeroVector;
	TWeakObjectPtr<AActor> KnockbackProbeWall;
	TMap<int32, TSet<TWeakObjectPtr<APlayerController>>> KnockbackConfirmations;
	UFUNCTION(Client, Reliable) void ClientObserveStun(APawn* Player, APawn* NPC, int32 Checkpoint);
	UFUNCTION(Server, Reliable) void ServerConfirmStun(int32 Checkpoint);
	void TickStunServer();
	void TickStunClient();
	bool ApplyStunProbeDamage(bool bPlayerSource, float Seconds);
	void SendStunProbeStep(int32 Checkpoint);
	bool bStunScenario = false;
	int32 StunStage = -1;
	int32 StunExpected = -1;
	int32 StunConfirmed = -1;
	int32 StunLocalInputStep = -1;
	int32 StunViolationBaseline = 0;
	double StunInputAt = 0.0;
	double StunDiagnosticAt = 0.0;
	FVector StunOrigin = FVector::ZeroVector;
	bool bStunAttemptedInput = false;
	TMap<int32, TSet<TWeakObjectPtr<APlayerController>>> StunConfirmations;
	UFUNCTION(Client, Reliable)
	void ClientSetLightHitSubjects(APawn* Player, APawn* NPC);
	UFUNCTION(Client, Reliable)
	void ClientExpectLightHit(APawn* Victim, int32 Checkpoint, uint32 Sequence, FName Section);
	UFUNCTION(Server, Reliable)
	void ServerConfirmLightHit(int32 Checkpoint);
	void TickLightHitServer();
	void TickLightHitClient();
	void TickLightHitInputServer();
	void TickLightHitInputClient();
	void CleanupLightHitInput();
	void HandleLightHitInputEnded(const FAbilityEndedData& Data);
	bool ApplyLightHitInputDamage();
	void SendLightHitInputStep(int32 Step);
	bool bLightHitInputScenario = false;
	bool bLightInputPrepared = false;
	bool bLightInputBound = false;
	bool bLightInputSent = false;
	bool bLightInputCancelled = false;
	int32 LightInputStartedStep = INDEX_NONE;
	int32 LightInputMontageId = INDEX_NONE;
	int32 LightInputAttackEnds = 0;
	int32 LightInputRollEnds = 0;
	int32 LightInputViolationBaseline = 0;
	double LightInputStartedAt = 0.0;
	FVector LightInputRollOrigin = FVector::ZeroVector;
	FGameplayAbilitySpecHandle LightInputRollAbility;
	FDelegateHandle LightInputEndedHandle;
	TWeakObjectPtr<UAbilitySystemComponent> LightInputASC;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> LightInputSubsystem;
	TWeakObjectPtr<const UInputAction> LightInputAttackAction;
	TWeakObjectPtr<const UInputAction> LightInputRollAction;
	TWeakObjectPtr<const UInputAction> LightInputMoveAction;
	bool bLightHitScenario = false;
	TWeakObjectPtr<APawn> LightHitPlayer;
	TWeakObjectPtr<APawn> LightHitNPC;
	TWeakObjectPtr<APawn> LightHitExpectedVictim;
	FGameplayAbilitySpecHandle LightHitPlayerAbility;
	FGameplayAbilitySpecHandle LightHitNPCAbility;
	int32 LightHitStep = 0;
	int32 LightHitExpectedCheckpoint = 0;
	int32 LightHitLastConfirmed = -1;
	uint32 LightHitExpectedSequence = 0;
	FName LightHitExpectedSection;
	double LightHitNextAt = 0.0;
	TMap<int32, TSet<TWeakObjectPtr<APlayerController>>> LightHitConfirmations;

	UFUNCTION(Server, Reliable)
	void ServerConfirmSnapshot(int32 Phase);
	UFUNCTION(Client, Reliable)
	void ClientAllowRetry();
	UFUNCTION(Server, Reliable)
	void ServerConfirmCombatPhase(int32 Checkpoint, int32 Sequence);
	UFUNCTION(Client, Reliable)
	void ClientObserveDodge(APawn* Target, int32 Sequence, FVector Center, double EndTime, float TargetHealth, FVector Origin);
	UFUNCTION(Client, Reliable)
	void ClientStartDodge();
	UFUNCTION(Server, Reliable)
	void ServerConfirmDodge(int32 Checkpoint, int32 Sequence);
	UFUNCTION(Client, Reliable)
	void ClientBeginPlayerAttack(APawn* Target, float BossHealth, APawn* Victim);
	UFUNCTION(Server, Reliable)
	void ServerConfirmPlayerAttack(int32 Checkpoint);
	bool PreparePlayerAttackLoadouts();
	void TickPlayerAttackServer();
	void TickPlayerAttackLocalInput();
	void TickPlayerAttackObserver();
	void CleanupPlayerAttackProbe();
	void HandlePlayerAttackEnded(const FAbilityEndedData& Data);
	void OnPlayerAttackEffectApplied(UAbilitySystemComponent* SourceASC,
		const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle);
	bool PrepareDodgeAbilities();
	void TickDodgeServer();
	void TickDodgeLocalInput();
	void TickDodgeObserver();
	void HandleDodgeEnded(const FAbilityEndedData& Data);
	void HandleDodgeStrikeEnded(const FAbilityEndedData& Data);
	void CleanupDodgeProbe();
	void TickServer();
	void TickClient();
	void TickNaturalCombatServer();
	void TickNaturalCombatClient();
	bool ObserveBossAttackPresentation(FGameplayTag Action);
	bool bSawBossStrikeMontage = false;
	bool bSawBossHeavyMontage = false;
	void TickCombatPhaseServer();
	void TickCombatPhaseClient();
	void TickSummonMechanicServer();
	void TickSummonMechanicClient();
	void ConfirmSummonMechanic(int32 Checkpoint, int32 Sequence);
	void OnSummonMechanicEffectApplied(UAbilitySystemComponent* SourceASC,
		const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle, UAbilitySystemComponent* TargetASC);
	void ConfirmSummonPlayerAttack(int32 Checkpoint);
	void OnSummonPlayerAttackActivated(UGameplayAbility* Ability);
	void CleanupSummonPlayerTargetObservation();
	FDelegateHandle SummonPlayerActivatedHandle;
	FDelegateHandle SummonPlayerTargetDataHandle;
	FPredictionKey SummonPlayerPredictionKey;
	void OnSummonPlayerKillEffectApplied(UAbilitySystemComponent* SourceASC,
		const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle);
	bool bSummonMechanicScenario = false;
	bool bSummonPlayerKillScenario = false;
	bool bSummonPlayerKillHit = false;
	bool bSummonMechanicDamage = false;
	bool bSummonMechanicClientDamage = false;
	int32 SummonMechanicStep = 0;
	double SummonGuardianDiagnosticAt = 0.0;
	int32 SummonMechanicSequence = 0;
	int32 SummonMechanicClientStep = 0;
	TArray<TWeakObjectPtr<class ARPGNonPlayerCharacter>> SummonMechanicMembers;
	TMap<TWeakObjectPtr<class ARPGNonPlayerCharacter>, FVector> SummonMechanicOrigins;
	TSet<TWeakObjectPtr<APlayerController>> SummonMechanicReadyClients;
	TSet<TWeakObjectPtr<APlayerController>> SummonMechanicPartialClients;
	TSet<TWeakObjectPtr<APlayerController>> SummonMechanicCompleteClients;
	void CleanupCombatPhaseProbe();
	bool PrepareBossAreaParticipants();
	void OnCombatPhaseEffectApplied(UAbilitySystemComponent* SourceASC,
		const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle, UAbilitySystemComponent* TargetASC);
	void Fail(const TCHAR* Reason);
	UUserWidget* FindResultWidget() const;
	bool bEnabled = false;
	bool bFinished = false;
	bool bAllowRetry = false;
	bool bClickedRetry = false;
	bool bStartConfirmed = false;
	bool bDeathAbilityScenario = false;
	bool bNaturalCombatScenario = false;
	bool bCombatPhaseScenario = false;
	bool bBossAreaStrikeScenario = false;
	bool bAreaParticipantsPositioned = false;
	uint64 AreaStrikeExecutionSerial = 0;
	uint64 AreaStrikeDamageFrame = MAX_uint64;
	TSet<TWeakObjectPtr<APawn>> AreaParticipants;
	TSet<TWeakObjectPtr<APawn>> AreaDamagedParticipants;
	bool bDodgeScenario = false;
	bool bPlayerAttackScenario = false;
	bool bPlayerAttackRequested = false;
	bool bPlayerAttackInputSent = false;
	bool bPlayerAttackReadyReported = false;
	bool bPlayerAttackObservedActive = false;
	bool bPlayerAttackObservedMontage = false;
	bool bPlayerAttackEndedNaturally = false;
	bool bPlayerAttackInputComplete = false;
	bool bPlayerAttackSawDamage = false;
	int32 PlayerAttackDamageCount = 0;
	bool bPlayerAttackClientObserved = false;
	bool bPlayerAttackOwnerConfirmed = false;
	float PlayerAttackBossHealth = 0.f;
	float PlayerAttackBossAfter = 0.f;
	double PlayerAttackThreatBefore = 0.0;
	double PlayerAttackInputStart = 0.0;
	double PlayerAttackPressStart = 0.0;
	double PlayerAttackDiagnosticAt = 0.0;
	int32 PlayerAttackViolationBaseline = 0;
	TWeakObjectPtr<APawn> PlayerAttackTarget;
	TWeakObjectPtr<class ACharacter> PlayerAttackVictim;
	TWeakObjectPtr<UAbilitySystemComponent> PlayerAttackASC;
	FGameplayAbilitySpecHandle PlayerAttackHandle;
	FDelegateHandle PlayerAttackEndedHandle;
	FDelegateHandle PlayerAttackEffectHandle;
	FDelegateHandle PlayerAttackHitEventHandle;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> PlayerAttackInputSubsystem;
	TWeakObjectPtr<const UInputAction> PlayerAttackMoveAction;
	TWeakObjectPtr<const UInputAction> PlayerAttackAction;
	TSet<TWeakObjectPtr<APlayerController>> PlayerAttackReadyClients;
	TSet<TWeakObjectPtr<APlayerController>> PlayerAttackObserverClients;
	bool bDodgeInputRequested = false;
	bool bDodgeInputSent = false;
	bool bDodgeReadyReported = false;
	bool bDodgeObservedActive = false;
	bool bDodgeObservedMontage = false;
	bool bDodgeObservedAuthorization = false;
	bool bDodgeEndedNaturally = false;
	bool bDodgeInputCompleted = false;
	bool bDodgeServerValidated = false;
	bool bDodgeStrikeEndedNaturally = false;
	bool bDodgeObserverCompleted = false;
	int32 DodgeSequence = 0;
	double DodgeEndTime = 0.0;
	double DodgeInputStartTime = 0.0;
	float DodgeHealth = 0.f;
	int32 DodgeViolationBaseline = 0;
	FVector DodgeCenter = FVector::ZeroVector;
	FVector DodgeOrigin = FVector::ZeroVector;
	TWeakObjectPtr<APawn> DodgeTarget;
	TWeakObjectPtr<UAbilitySystemComponent> DodgeASC;
	FGameplayAbilitySpecHandle DodgeRollHandle;
	FGameplayAbilitySpecHandle DodgeStrikeHandle;
	FDelegateHandle DodgeEndedHandle;
	FDelegateHandle DodgeStrikeEndedHandle;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> DodgeInputSubsystem;
	TWeakObjectPtr<const UInputAction> DodgeMoveAction;
	TWeakObjectPtr<const UInputAction> DodgeRollAction;
	TSet<TWeakObjectPtr<APlayerController>> DodgeReadyClients;
	TSet<TWeakObjectPtr<APlayerController>> DodgeObserverClients;
	bool bDodgeTargetInputConfirmed = false;
	int32 FirstCombatSequence = 0;
	int32 CombatPhaseStep = 0;
	int32 ClientCombatPhaseStep = 0;
	int32 TransitionSequence = 0;
	int32 HeavySequence = 0;
	void ObserveVariantHazard();
	int32 VariantHazardSequence = 0;
	bool bSawVariantHazardActor = false;
	bool bSawVariantHazardDamage = false;
	TWeakObjectPtr<class ARPGBossPersistentHazard> VariantHazard;
	TSet<TWeakObjectPtr<APlayerController>> VariantHazardClients;
	double CancelledStrikeEnd = 0.0;
	bool bOldStrikeWindowPassed = false;
	bool bSawHeavyGASDamage = false;
	TMap<TWeakObjectPtr<APawn>, float> PhaseCombatHealth;
	TSet<TWeakObjectPtr<APlayerController>> PhaseWindupClients;
	TSet<TWeakObjectPtr<APlayerController>> HeavyReadyClients;
	TSet<TWeakObjectPtr<APlayerController>> HeavyDamageClients;
	TArray<TPair<TWeakObjectPtr<UAbilitySystemComponent>, FDelegateHandle>> PhaseEffectBindings;
	bool bSawCombatTelegraph = false;
	bool bSawCombatDamage = false;
	FVector CombatBossStart = FVector::ZeroVector;
	TMap<TWeakObjectPtr<APawn>, float> CombatHealth;
	TSet<TWeakObjectPtr<APlayerController>> CombatClients;
	int32 ServerStage = 0;
	int32 ClientPhase = 0;
	int32 DefeatRevision = 0;
	double Deadline = 0.0;
	double NextDiagnosticAt = 0.0;
	double ConfirmStartAt = 0.0;
	TSet<TWeakObjectPtr<APlayerController>> ReadyClients;
	TSet<TWeakObjectPtr<APlayerController>> DefeatedClients;
	TSet<TWeakObjectPtr<APlayerController>> RecoveredClients;
	TWeakObjectPtr<ARPGBossCharacter> OriginalBoss;
	TWeakObjectPtr<APawn> OriginalPawn;
	TArray<TWeakObjectPtr<AActor>> OriginalStartupWeapons;
	double PlayerAttackAimStart = 0.0;
	double PlayerAttackChargeStart = 0.0;
	bool bPlayerAttackChargeHeld = false;
	bool bPlayerAttackChargeReleased = false;
	bool bPlayerAttackChargeInputReleased = false;
	TArray<TWeakObjectPtr<UAbilitySystemComponent>> PlayerAttackRageFixtureASCs;
	bool bPlayerAttackDurabilityFixturePrepared = false;
	double PlayerAttackChannelStart = 0.0;
	double PlayerAttackChannelLastHit = 0.0;
	double PlayerAttackChannelQuietStart = 0.0;
	float PlayerAttackChannelLoopSeconds = 0.f;
	float PlayerAttackChannelStopHP = 0.f;
	float PlayerAttackChannelClientHP = 0.f;
	int32 PlayerAttackChannelClientDrops = 0;
	bool bPlayerAttackChannelHeld = false;
	bool bPlayerAttackChannelStopped = false;
	bool bPlayerAttackChannelInputReleased = false;
	float PlayerAttackChannelPreviousAdvance = 0.f;
	uint64 PlayerAttackChannelHitFrame = MAX_uint64;
	int32 PlayerAttackChannelHitsInFrame = 0;
	int32 PlayerAttackChannelMontageInstanceID = INDEX_NONE;
public:
	/** Development opt-in fixture; run after NPC startup but before encounter start. */
	bool PreparePlayerAttackBossFixture(ARPGBossCharacter* Boss);
private:
	UFUNCTION(Server, Reliable)
	void ServerConfirmBossDeath(int32 Checkpoint, int32 Sequence);
	void TickBossDeathServer();
	void TickBossDeathClient();
	void CleanupBossDeathProbe();
	void OnBossDeathAbilityActivated(UGameplayAbility* Ability);
	void OnBossDeathAbilityEnded(const FAbilityEndedData& Data);
	bool bBossDeathScenario = false;
	int32 BossDeathStep = 0;
	int32 BossDeathSequence = 0;
	int32 BossDeathActivatedCount = 0;
	int32 BossDeathEndedCount = 0;
	double BossDeathOldStrikeEnd = 0.0;
	double BossDeathQuietStart = 0.0;
	bool bBossDeathServerMontage = false;
	bool bBossDeathClientMontage = false;
	bool bBossDeathClientConfirmed = false;
	FGameplayAbilitySpecHandle BossDeathHandle;
	FGameplayAbilitySpecHandle BossDeathStrikeHandle;
	TWeakObjectPtr<UAbilitySystemComponent> BossDeathASC;
	TWeakObjectPtr<APawn> BossDeathSource;
	FDelegateHandle BossDeathActivatedBinding;
	FDelegateHandle BossDeathEndedBinding;
	TSet<TWeakObjectPtr<APlayerController>> BossDeathWindupClients;
	TSet<TWeakObjectPtr<APlayerController>> BossDeathClients;
	TMap<TWeakObjectPtr<APawn>, float> BossDeathParticipantHealth;
};
