#pragma once

#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGMovementSecurityE2EProbeComponent.generated.h"

class ACharacter;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class URPGSecurityValidationComponent;
class UAbilityTask_AuthorizedMovement;
class URPGMovementAuthorizationTestAbility;
class UAbilitySystemComponent;
struct FAbilityEndedData;

/** Opt-in development probe: authored move input, server observations, positive control. */
UCLASS(Transient, NotBlueprintable)
class URPGMovementSecurityE2EProbeComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGMovementSecurityE2EProbeComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	enum class EServerPhase : uint8 { Baseline, WaitForJump, Dash, Knockback, Overlap, AuthoredRoll };
	bool VerifyPacketSimulation();
	bool PrepareClientInput();
	void TickClient(double Now);
	void TickServer(ACharacter& Character, double Now);
	void TickAuthorizedServer(ACharacter& Character, double Now);
	bool PrepareRollAbility(ACharacter& Character);
	bool PrepareRollInput();
	void TickRollClient(ACharacter& Character, double Now);
	void TickRollServer(ACharacter& Character, double Now);
	void HandleRollEnded(const FAbilityEndedData& EndData);
	UFUNCTION(Server, Reliable)
	void ServerReportRollClientReady();
	void TryScheduleConcurrentRoll(double Now);
	bool ObserveConcurrentRolls();
	bool CancelRollWithPeerIsolation(double Now);
	bool PrepareTestAbility(ACharacter& Character);
	FGuid BeginBlueprintAuthorization(URPGSecurityValidationComponent& Component,
		float Duration, float Distance, FName Reason);
	UAbilityTask_AuthorizedMovement* MakeWindow(float Duration, float Distance, FName Reason);
	void StartPositiveControl(ACharacter& Character, double Now);
	UFUNCTION()
	void HandleDashAuthorized();
	UFUNCTION()
	void HandleTaskCancelled();
	UFUNCTION()
	void HandleTaskFinished();
	UFUNCTION()
	void HandleImmediateCancellation();
	void FinishProbe();
	void FailProbe(const TCHAR* Reason);
	UFUNCTION()
	void HandleViolation(const FRPGSecurityViolation& Violation);

	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
	TWeakObjectPtr<const UInputAction> MoveAction;
	TWeakObjectPtr<const UInputAction> RollAction;
	TWeakObjectPtr<UAbilitySystemComponent> RollAbilitySystem;
	FGameplayAbilitySpecHandle RollHandle;
	FDelegateHandle RollEndedHandle;
	// Owner-only development test state; remains zero without the multi-client opt-in.
	UPROPERTY(Replicated)
	double ScheduledRollInputStartAt = 0.0;
	int32 TestPlayerId = INDEX_NONE;
	int32 ExpectedClientCount = 1;
	int32 MaximumConcurrentRollPlayers = 0;
	int32 PeerIsolationChecks = 0;
	bool bRollClientReady = false;
	bool bRollReadyReported = false;
	int32 RollInputsSent = 0;
	int32 RollCompletions = 0;
	int32 RollMontageSamples = 0;
	double RollStartedAt = 0.0;
	double RollDistance = 0.0;
	double RollInputReleaseAt = 0.0;
	bool bTestAuthoredRoll = false;
	bool bRollObservedActive = false;
	bool bRollObservedAuthorization = false;
	bool bRollObservedBudgetUse = false;
	bool bRollCancellationRequested = false;
	bool bRollEnded = false;
	bool bRollEndedCancelled = false;
	bool bClientRollCompleted = false;
	TWeakObjectPtr<ACharacter> ObservedCharacter;
	TWeakObjectPtr<URPGSecurityValidationComponent> Security;
	UPROPERTY()
	TObjectPtr<URPGMovementAuthorizationTestAbility> TestAbility;
	UPROPERTY()
	TObjectPtr<UAbilityTask_AuthorizedMovement> DashTask;
	UPROPERTY()
	TObjectPtr<UAbilityTask_AuthorizedMovement> OlderTask;
	UPROPERTY()
	TObjectPtr<UAbilityTask_AuthorizedMovement> NewerTask;
	UPROPERTY()
	TObjectPtr<UAbilityTask_AuthorizedMovement> ImmediateCancelTask;
	FVector LastServerLocation = FVector::ZeroVector;
	FVector PhaseOrigin = FVector::ZeroVector;
	EServerPhase ServerPhase = EServerPhase::Baseline;
	double PhaseStartedAt = 0.0;
	double JumpHeight = 0.0;
	double KnockbackDistance = 0.0;
	int32 AirborneSamples = 0;
	int32 CancelledTasks = 0;
	int32 FinishedTasks = 0;
	bool bTestAuthorizedMovement = false;
	bool bClientJumped = false;
	bool bKnockbackObserved = false;
	double Deadline = 0.0;
	double StartTime = 0.0;
	double WarmupEndsAt = 0.0;
	double PositiveControlDeadline = 0.0;
	double ServerDistance = 0.0;
	int32 ObservedSamples = 0;
	int32 WalkingSamples = 0;
	int32 FalsePositiveCount = 0;
	bool bVerifiedSimulation = false;
	bool bStarted = false;
	bool bPositiveControl = false;
	bool bPositiveControlObserved = false;
	bool bFinished = false;
};
