#include "Tests/RPGMovementSecurityE2EProbeComponent.h"

#include "Tests/RPGMovementAuthorizationTestAbility.h"
#include "Ability/AbilityTask/AbilityTask_AuthorizedMovement.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "RPGInputTags.h"
#include "UObject/Class.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGMovementSecurityE2EProbeComponent)

DEFINE_LOG_CATEGORY_STATIC(LogRPGMovementSecurityE2E, Log, All);

namespace
{
	constexpr double MeasurementSeconds = 12.0;
	constexpr double ClientInputSeconds = 24.0;
	constexpr double MinimumServerDistance = 1500.0;
}

URPGMovementSecurityE2EProbeComponent::URPGMovementSecurityE2EProbeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// Iris includes native replicated default subobjects in the controller's
	// protocol. Keep CDO/instance layouts identical; never flip this in BeginPlay.
	// The controller creates this probe only in non-Shipping builds, and the
	// owner-only scheduling field stays zero outside an opt-in multi-client test.
	SetIsReplicatedByDefault(true);
}

void URPGMovementSecurityE2EProbeComponent::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGMovementSecurityE2E")))
	{
		Deadline = FPlatformTime::Seconds() + 75.0;
		bTestAuthorizedMovement = FParse::Param(FCommandLine::Get(), TEXT("RPGMovementSecurityAuthorizedE2E"));
		bTestAuthoredRoll = FParse::Param(FCommandLine::Get(), TEXT("RPGMovementSecurityRollE2E"));
		FParse::Value(FCommandLine::Get(), TEXT("RPGExpectedClients="), ExpectedClientCount);
		if (ExpectedClientCount < 1 || ExpectedClientCount > 16)
		{
			FailProbe(TEXT("InvalidExpectedClientCount"));
			return;
		}
		if (bTestAuthoredRoll && ExpectedClientCount > 1)
		{
			Deadline = FPlatformTime::Seconds() + 180.0;
		}
		SetComponentTickEnabled(true);
	}
#endif
}

void URPGMovementSecurityE2EProbeComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, ScheduledRollInputStartAt, COND_OwnerOnly);
}

void URPGMovementSecurityE2EProbeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	FinishProbe();
	Super::EndPlay(Reason);
}

void URPGMovementSecurityE2EProbeComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if !UE_BUILD_SHIPPING
	if (bFinished)
	{
		return;
	}
	if (FPlatformTime::Seconds() >= Deadline)
	{
		FailProbe(TEXT("Timeout"));
		return;
	}
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
	if (!Character || !Character->IsPlayerControlled())
	{
		return;
	}
	if (!bVerifiedSimulation)
	{
		if (!VerifyPacketSimulation())
		{
			return;
		}
		bVerifiedSimulation = true;
	}
	if (!Controller->PlayerState || Controller->PlayerState->GetPlayerId() <= 0)
	{
		return;
	}
	const int32 PlayerId = Controller->PlayerState->GetPlayerId();
	if (TestPlayerId == INDEX_NONE)
	{
		TestPlayerId = PlayerId;
		UE_LOG(LogRPGMovementSecurityE2E, Display,
			TEXT("RPG_MOVEMENT_NETTEST PEER_IDENTIFIED Role=%s PlayerId=%d"),
			GetNetMode() == NM_Client ? TEXT("Client") : TEXT("Server"), TestPlayerId);
	}
	else if (TestPlayerId != PlayerId)
	{
		FailProbe(TEXT("PlayerIdentityChanged"));
		return;
	}
	double Now = GetWorld()->GetTimeSeconds();
	if (Controller->GetNetMode() == NM_Client && Controller->IsLocalController())
	{
		if (bTestAuthoredRoll && ExpectedClientCount > 1)
		{
			const AGameStateBase* GameState = GetWorld()->GetGameState();
			if (!GameState)
			{
				return;
			}
			Now = GameState->GetServerWorldTimeSeconds();
		}
		TickClient(Now);
	}
	else if (Controller->GetNetMode() == NM_DedicatedServer)
	{
		TickServer(*Character, Now);
	}
	else
	{
		FailProbe(TEXT("RequiresDedicatedServerAndRemoteClient"));
	}
#endif
}

bool URPGMovementSecurityE2EProbeComponent::VerifyPacketSimulation()
{
#if !UE_BUILD_SHIPPING && DO_ENABLE_NET_TEST
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!Driver)
	{
		return false;
	}
	int32 ExpectedMin = 0;
	int32 ExpectedMax = 0;
	int32 ExpectedLoss = 0;
	FParse::Value(FCommandLine::Get(), TEXT("PktLagMin="), ExpectedMin);
	FParse::Value(FCommandLine::Get(), TEXT("PktLagMax="), ExpectedMax);
	FParse::Value(FCommandLine::Get(), TEXT("PktLoss="), ExpectedLoss);
	const FPacketSimulationSettings& Settings = Driver->PacketSimulationSettings;
	if (!Driver->IsUsingIrisReplication() || Settings.PktLagMin != ExpectedMin ||
		Settings.PktLagMax != ExpectedMax || Settings.PktLoss != ExpectedLoss ||
		Settings.PktLag != 0 || Settings.PktOrder != 0 || Settings.PktDup != 0 ||
		Settings.PktIncomingLagMin != 0 || Settings.PktIncomingLagMax != 0 ||
		Settings.PktIncomingLoss != 0)
	{
		FailProbe(TEXT("PacketSimulationMismatch"));
		return false;
	}
	UE_LOG(LogRPGMovementSecurityE2E, Display,
		TEXT("RPG_MOVEMENT_NETTEST SIMULATION Role=%s LagMin=%d LagMax=%d Loss=%d Iris=1"),
		GetNetMode() == NM_Client ? TEXT("Client") : TEXT("Server"),
		Settings.PktLagMin, Settings.PktLagMax, Settings.PktLoss);
	return true;
#else
	FailProbe(TEXT("PacketSimulationUnavailable"));
	return false;
#endif
}

bool URPGMovementSecurityE2EProbeComponent::PrepareClientInput()
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->GetLocalPlayer())
	{
		return false;
	}
	const UDataAsset_InputConfig* Config = LoadObject<UDataAsset_InputConfig>(nullptr,
		TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
	const UInputAction* Action = Config
		? Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
	const UEnhancedInputComponent* Bindings = Cast<UEnhancedInputComponent>(Controller->InputComponent);
	if (!Config || !Action || Action->ValueType != EInputActionValueType::Axis2D)
	{
		FailProbe(TEXT("MoveInputConfigMissing"));
		return false;
	}
	if (!Subsystem || !Bindings || !Subsystem->HasMappingContext(Config->DefaultMappingContext))
	{
		return false;
	}
	const bool bBound = Bindings->GetActionEventBindings().ContainsByPredicate(
		[Action](const TUniquePtr<FEnhancedInputActionEventBinding>& Binding)
		{
			return Binding && Binding->GetAction() == Action &&
				Binding->GetTriggerEvent() == ETriggerEvent::Triggered;
		});
	if (!bBound)
	{
		FailProbe(TEXT("MoveInputNotBound"));
		return false;
	}
	InputSubsystem = Subsystem;
	MoveAction = Action;
	return true;
}

void URPGMovementSecurityE2EProbeComponent::TickClient(const double Now)
{
	if (!bStarted)
	{
		if (!PrepareClientInput())
		{
			return;
		}
		bStarted = true;
		StartTime = Now;
		if (bTestAuthorizedMovement)
		{
			ACharacter* Character = Cast<ACharacter>(CastChecked<APlayerController>(GetOwner())->GetPawn());
			URPGSecurityValidationComponent* ClientSecurity =
				Character->FindComponentByClass<URPGSecurityValidationComponent>();
			if (!ClientSecurity || BeginBlueprintAuthorization(*ClientSecurity, 1.0f, 1000.0f,
				TEXT("UntrustedClient")).IsValid() || ClientSecurity->IsMovementAuthorizationActive())
			{
				FailProbe(TEXT("ClientGrantedMovementAuthorization"));
				return;
			}
			UE_LOG(LogRPGMovementSecurityE2E, Display,
				TEXT("RPG_MOVEMENT_NETTEST CLIENT_AUTHORITY_REJECTED"));
		}
		if (bTestAuthoredRoll)
		{
			ACharacter* Character = Cast<ACharacter>(CastChecked<APlayerController>(GetOwner())->GetPawn());
			if (!PrepareRollAbility(*Character) || !PrepareRollInput())
			{
				bStarted = false;
				return;
			}
			if (ExpectedClientCount > 1)
			{
				if (!bRollReadyReported)
				{
					bRollReadyReported = true;
					ServerReportRollClientReady();
				}
				if (ScheduledRollInputStartAt <= 0.0 || Now < ScheduledRollInputStartAt)
				{
					bStarted = false;
					return;
				}
				StartTime = ScheduledRollInputStartAt;
			}
		}
		InputSubsystem->StartContinuousInputInjectionForAction(
			MoveAction.Get(), FInputActionValue(FVector2D(0.0, 1.0)), {}, {});
		UE_LOG(LogRPGMovementSecurityE2E, Display,
			TEXT("RPG_MOVEMENT_NETTEST CLIENT_INPUT_STARTED Action=%s PlayerId=%d"),
			*GetNameSafe(MoveAction.Get()), TestPlayerId);
	}
	if (bTestAuthorizedMovement && Now - StartTime >= 15.0)
	{
		ACharacter* Character = Cast<ACharacter>(CastChecked<APlayerController>(GetOwner())->GetPawn());
		if (!bClientJumped)
		{
			Character->Jump();
			bClientJumped = true;
			UE_LOG(LogRPGMovementSecurityE2E, Display, TEXT("RPG_MOVEMENT_NETTEST CLIENT_JUMP_STARTED"));
		}
		else if (Now - StartTime >= 15.3)
		{
			Character->StopJumping();
		}
	}
	if (bTestAuthoredRoll)
	{
		ACharacter* Character = Cast<ACharacter>(CastChecked<APlayerController>(GetOwner())->GetPawn());
		TickRollClient(*Character, Now);
		if (bFinished)
		{
			return;
		}
	}
	if (Now - StartTime >= (bTestAuthorizedMovement ? 42.0 : bTestAuthoredRoll ? 28.0 : ClientInputSeconds))
	{
		if (bTestAuthoredRoll && !bClientRollCompleted)
		{
			FailProbe(TEXT("AuthoredRollClientIncomplete"));
			return;
		}
		UE_LOG(LogRPGMovementSecurityE2E, Display,
			TEXT("RPG_MOVEMENT_NETTEST CLIENT_COMPLETE Duration=%.2f PlayerId=%d"), Now - StartTime, TestPlayerId);
		FinishProbe();
		return;
	}
	const double Angle = (Now - StartTime) * UE_DOUBLE_PI / 2.0;
	InputSubsystem->UpdateValueOfContinuousInputInjectionForAction(
		MoveAction.Get(), FInputActionValue(FVector2D(FMath::Sin(Angle), FMath::Cos(Angle))));
}

void URPGMovementSecurityE2EProbeComponent::TickServer(ACharacter& Character, const double Now)
{
	if (!Security.IsValid())
	{
		Security = Character.FindComponentByClass<URPGSecurityValidationComponent>();
		if (!Security.IsValid() || !Security->IsComponentTickEnabled() ||
			!Security->GetEffectivePolicyConfig().Movement.bEnabled)
		{
			FailProbe(TEXT("MovementSecurityNotEnabled"));
			return;
		}
		ObservedCharacter = &Character;
		WarmupEndsAt = Now + Security->GetEffectivePolicyConfig().Movement.SpawnGraceSeconds + 0.5;
		LastServerLocation = Character.GetActorLocation();
		Security->OnViolationReported.AddDynamic(this, &ThisClass::HandleViolation);
		if (bTestAuthoredRoll && !PrepareRollAbility(Character))
		{
			return;
		}
	}
	if (ObservedCharacter.Get() != &Character)
	{
		FailProbe(TEXT("PawnChanged"));
		return;
	}
	if (bTestAuthoredRoll && ExpectedClientCount > 1 && ScheduledRollInputStartAt <= 0.0)
	{
		TryScheduleConcurrentRoll(Now);
	}
	if (bPositiveControl)
	{
		if (bPositiveControlObserved)
		{
			if (bTestAuthorizedMovement)
			{
				UE_LOG(LogRPGMovementSecurityE2E, Display,
					TEXT("RPG_MOVEMENT_NETTEST AUTHORIZED_COMPLETE Jump=1 Dash=1 Knockback=1 Overlap=1 ReentrantCancel=1 BudgetExceeded=1 Cancelled=%d Finished=%d PlayerId=%d"),
					CancelledTasks, FinishedTasks, TestPlayerId);
			}
			UE_LOG(LogRPGMovementSecurityE2E, Display,
				TEXT("RPG_MOVEMENT_NETTEST SERVER_COMPLETE FalsePositives=%d PositiveControl=1 PlayerId=%d"),
				FalsePositiveCount, TestPlayerId);
			FinishProbe();
		}
		else if (Now >= PositiveControlDeadline)
		{
			FailProbe(TEXT("PositiveControlNotDetected"));
		}
		return;
	}
	if (bTestAuthorizedMovement && ServerPhase != EServerPhase::Baseline)
	{
		TickAuthorizedServer(Character, Now);
		return;
	}
	if (bTestAuthoredRoll && ServerPhase == EServerPhase::AuthoredRoll)
	{
		TickRollServer(Character, Now);
		return;
	}
	UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	if (!Movement || (Character.GetMesh() && Character.GetMesh()->IsPlayingRootMotion()) ||
		Security->IsMovementAuthorizationActive())
	{
		FailProbe(TEXT("UnexpectedMovementExemption"));
		return;
	}
	const FVector Location = Character.GetActorLocation();
	const double Step = FVector::Dist2D(Location, LastServerLocation);
	LastServerLocation = Location;
	if (!bStarted)
	{
		if (Now < WarmupEndsAt || Step < 1.0)
		{
			return;
		}
		bStarted = true;
		StartTime = Now;
	}
	ServerDistance += Step;
	++ObservedSamples;
	WalkingSamples += Movement->IsMovingOnGround() ? 1 : 0;
	if (Now - StartTime < MeasurementSeconds)
	{
		return;
	}
	UE_LOG(LogRPGMovementSecurityE2E, Display,
		TEXT("RPG_MOVEMENT_NETTEST NORMAL_RESULT Duration=%.2f Distance=%.1f Samples=%d Walking=%d FalsePositives=%d PlayerId=%d"),
		Now - StartTime, ServerDistance, ObservedSamples, WalkingSamples, FalsePositiveCount, TestPlayerId);
	if (FalsePositiveCount != 0 || ServerDistance < MinimumServerDistance ||
		ObservedSamples < 60 || WalkingSamples < ObservedSamples / 2)
	{
		FailProbe(TEXT("NormalMovementCoverageOrFalsePositive"));
		return;
	}
	if (bTestAuthorizedMovement)
	{
		if (!PrepareTestAbility(Character))
		{
			return;
		}
		ServerPhase = EServerPhase::WaitForJump;
		PhaseStartedAt = Now;
		PhaseOrigin = Location;
		return;
	}
	if (bTestAuthoredRoll)
	{
		ServerPhase = EServerPhase::AuthoredRoll;
		PhaseStartedAt = Now;
		return;
	}
	StartPositiveControl(Character, Now);
}

bool URPGMovementSecurityE2EProbeComponent::PrepareRollAbility(ACharacter& Character)
{
	if (RollAbilitySystem.IsValid() && RollHandle.IsValid())
	{
		return true;
	}
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(&Character);
	if (!ASC)
	{
		FailProbe(TEXT("RollAbilitySystemMissing"));
		return false;
	}
	FGameplayAbilitySpec* RollSpec = ASC->GetActivatableAbilities().FindByPredicate(
		[](const FGameplayAbilitySpec& Spec)
		{
			return Spec.Ability && Spec.Ability->GetClass()->GetPathName() ==
				TEXT("/Game/Blueprints/Character/Player/Abilities/GA_Player_Roll.GA_Player_Roll_C");
		});
	if (!RollSpec && Character.HasAuthority())
	{
		UClass* RollClass = LoadClass<UGameplayAbility>(nullptr,
			TEXT("/Game/Blueprints/Character/Player/Abilities/GA_Player_Roll.GA_Player_Roll_C"));
		if (!RollClass)
		{
			FailProbe(TEXT("AuthoredRollClassMissing"));
			return false;
		}
		FGameplayAbilitySpec Grant(RollClass, 1);
		Grant.SourceObject = &Character;
		Grant.GetDynamicSpecSourceTags().AddTag(RPGGameplayTags::InputTag_Roll);
		RollSpec = ASC->FindAbilitySpecFromHandle(ASC->GiveAbility(Grant));
	}
	if (!RollSpec)
	{
		return false;
	}
	if (!RollSpec->GetDynamicSpecSourceTags().HasTagExact(RPGGameplayTags::InputTag_Roll))
	{
		FailProbe(TEXT("AuthoredRollInputTagMissing"));
		return false;
	}
	RollAbilitySystem = ASC;
	RollHandle = RollSpec->Handle;
	RollEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleRollEnded);
	return true;
}

bool URPGMovementSecurityE2EProbeComponent::PrepareRollInput()
{
	const UDataAsset_InputConfig* Config = LoadObject<UDataAsset_InputConfig>(nullptr,
		TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
	const UInputAction* Action = Config ? Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_Roll) : nullptr;
	APlayerController* Controller = CastChecked<APlayerController>(GetOwner());
	const UEnhancedInputComponent* Bindings = Cast<UEnhancedInputComponent>(Controller->InputComponent);
	bool bPressed = false;
	bool bReleased = false;
	if (Bindings && Action)
	{
		for (const auto& Binding : Bindings->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action)
			{
				bPressed |= Binding->GetTriggerEvent() == ETriggerEvent::Started;
				bReleased |= Binding->GetTriggerEvent() == ETriggerEvent::Completed;
			}
		}
	}
	if (!Action || Action->ValueType != EInputActionValueType::Boolean || !bPressed || !bReleased)
	{
		FailProbe(TEXT("AuthoredRollInputNotBound"));
		return false;
	}
	RollAction = Action;
	return true;
}

void URPGMovementSecurityE2EProbeComponent::HandleRollEnded(const FAbilityEndedData& EndData)
{
	if (EndData.AbilitySpecHandle == RollHandle)
	{
		bRollEnded = true;
		bRollEndedCancelled = EndData.bWasCancelled;
	}
}

void URPGMovementSecurityE2EProbeComponent::ServerReportRollClientReady_Implementation()
{
	if (GetNetMode() != NM_DedicatedServer || !bTestAuthoredRoll || ExpectedClientCount <= 1 ||
		bFinished || !RollAbilitySystem.IsValid() || !RollHandle.IsValid() || !Security.IsValid())
	{
		return;
	}
	bRollClientReady = true;
}

void URPGMovementSecurityE2EProbeComponent::TryScheduleConcurrentRoll(const double Now)
{
	TArray<URPGMovementSecurityE2EProbeComponent*> Peers;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PeerController = It->Get();
		URPGMovementSecurityE2EProbeComponent* Peer = PeerController
			? PeerController->FindComponentByClass<URPGMovementSecurityE2EProbeComponent>() : nullptr;
		if (!Peer || !Peer->bTestAuthoredRoll || !Peer->bRollClientReady ||
			Peer->TestPlayerId == INDEX_NONE || !Peer->Security.IsValid() || Peer->bFinished)
		{
			return;
		}
		Peers.Add(Peer);
	}
	if (Peers.Num() != ExpectedClientCount)
	{
		return;
	}
	const double StartAt = Now + 5.0;
	for (URPGMovementSecurityE2EProbeComponent* Peer : Peers)
	{
		Peer->ScheduledRollInputStartAt = StartAt;
		Peer->GetOwner()->ForceNetUpdate();
	}
	UE_LOG(LogRPGMovementSecurityE2E, Display,
		TEXT("RPG_MOVEMENT_NETTEST ROLL_BARRIER Players=%d StartAt=%.3f"), Peers.Num(), StartAt);
}

bool URPGMovementSecurityE2EProbeComponent::ObserveConcurrentRolls()
{
	if (!Security->IsMovementAuthorizationActive() || !ObservedCharacter.IsValid() ||
		!ObservedCharacter->GetMesh() || !ObservedCharacter->GetMesh()->IsPlayingRootMotion())
	{
		return true;
	}
	int32 ActivePlayers = 1;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PeerController = It->Get();
		URPGMovementSecurityE2EProbeComponent* Peer = PeerController
			? PeerController->FindComponentByClass<URPGMovementSecurityE2EProbeComponent>() : nullptr;
		if (!Peer || Peer == this || !Peer->Security.IsValid() || !Peer->RollAbilitySystem.IsValid())
		{
			continue;
		}
		if (Peer->Security.Get() == Security.Get() ||
			Peer->Security->GetOwner() != Peer->ObservedCharacter.Get() ||
			Peer->RollAbilitySystem->GetAvatarActor() != Peer->ObservedCharacter.Get())
		{
			FailProbe(TEXT("RollSecurityOwnerIsolationFailed"));
			return false;
		}
		const FGameplayAbilitySpec* PeerSpec = Peer->RollAbilitySystem->FindAbilitySpecFromHandle(Peer->RollHandle);
		if (PeerSpec && PeerSpec->IsActive() && Peer->Security->IsMovementAuthorizationActive() &&
			Peer->Security->GetAuthorizedMovementReason() == TEXT("PlayerRoll") &&
			Peer->ObservedCharacter.IsValid() && Peer->ObservedCharacter->GetMesh() &&
			Peer->ObservedCharacter->GetMesh()->IsPlayingRootMotion())
		{
			++ActivePlayers;
		}
	}
	MaximumConcurrentRollPlayers = FMath::Max(MaximumConcurrentRollPlayers, ActivePlayers);
	return true;
}

bool URPGMovementSecurityE2EProbeComponent::CancelRollWithPeerIsolation(const double Now)
{
	struct FPeerSnapshot
	{
		URPGMovementSecurityE2EProbeComponent* Probe;
		float RemainingDistance;
	};
	TArray<FPeerSnapshot> ActivePeers;
	bool bIsolationAlreadyObserved = PeerIsolationChecks > 0;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PeerController = It->Get();
		URPGMovementSecurityE2EProbeComponent* Peer = PeerController
			? PeerController->FindComponentByClass<URPGMovementSecurityE2EProbeComponent>() : nullptr;
		const FGameplayAbilitySpec* PeerSpec = Peer && Peer->RollAbilitySystem.IsValid()
			? Peer->RollAbilitySystem->FindAbilitySpecFromHandle(Peer->RollHandle) : nullptr;
		bIsolationAlreadyObserved |= Peer && Peer->PeerIsolationChecks > 0;
		if (Peer && Peer != this && PeerSpec && PeerSpec->IsActive() && Peer->Security.IsValid() &&
			Peer->Security->IsMovementAuthorizationActive())
		{
			ActivePeers.Add({Peer, Peer->Security->GetRemainingAuthorizedMovementDistance()});
		}
	}
	// A common client input time does not align arrival of predicted activation
	// RPCs under loss. Give a late peer a bounded opportunity to join the second
	// Roll before cancelling; never prolong the actual ability or its approval.
	// Once any player has recorded actual isolation evidence, the final player
	// must not wait for already-cancelled peers and miss the montage's blend-out.
	// If no peer arrives, cancel anyway and let proof validation reject missing
	// isolation coverage. The single-client timing remains unchanged.
	if (ExpectedClientCount > 1 && !bIsolationAlreadyObserved && ActivePeers.IsEmpty() &&
		Now - RollStartedAt < 0.4)
	{
		return true;
	}
	bRollCancellationRequested = true;
	RollAbilitySystem->CancelAbilityHandle(RollHandle);
	for (const FPeerSnapshot& Snapshot : ActivePeers)
	{
		URPGMovementSecurityE2EProbeComponent* Peer = Snapshot.Probe;
		if (!IsValid(Peer) || !Peer->RollAbilitySystem.IsValid() || !Peer->Security.IsValid())
		{
			FailProbe(TEXT("RollCancellationRemovedAnotherPlayer"));
			return false;
		}
		const FGameplayAbilitySpec* PeerSpec = Peer->RollAbilitySystem->FindAbilitySpecFromHandle(Peer->RollHandle);
		if (!PeerSpec || !PeerSpec->IsActive() || !Peer->Security->IsMovementAuthorizationActive() ||
			Peer->Security->GetAuthorizedMovementReason() != TEXT("PlayerRoll") ||
			!FMath::IsNearlyEqual(Peer->Security->GetRemainingAuthorizedMovementDistance(), Snapshot.RemainingDistance))
		{
			FailProbe(TEXT("RollCancellationChangedAnotherPlayer"));
			return false;
		}
		++PeerIsolationChecks;
		UE_LOG(LogRPGMovementSecurityE2E, Display,
			TEXT("RPG_MOVEMENT_NETTEST ROLL_PEER_PRESERVED PlayerId=%d OtherPlayerId=%d Remaining=%.1f Preserved=1"),
			TestPlayerId, Peer->TestPlayerId, Snapshot.RemainingDistance);
	}
	return true;
}

void URPGMovementSecurityE2EProbeComponent::TickRollClient(ACharacter& Character, const double Now)
{
	if (bClientRollCompleted)
	{
		return;
	}
	if (!RollAbilitySystem.IsValid() && (!PrepareRollAbility(Character) || !PrepareRollInput()))
	{
		return;
	}
	if (RollInputReleaseAt > 0.0 && Now >= RollInputReleaseAt)
	{
		InputSubsystem->StopContinuousInputInjectionForAction(RollAction.Get());
		RollInputReleaseAt = 0.0;
	}
	FGameplayAbilitySpec* Spec = RollAbilitySystem->FindAbilitySpecFromHandle(RollHandle);
	if (!Spec)
	{
		FailProbe(TEXT("AuthoredRollSpecLost"));
		return;
	}
	if (RollInputsSent == RollCompletions && RollInputsSent < 2 &&
		Now - StartTime >= (RollInputsSent == 0 ? 15.0 : 19.0))
	{
		InputSubsystem->StartContinuousInputInjectionForAction(RollAction.Get(), FInputActionValue(true), {}, {});
		RollInputReleaseAt = Now + 0.25;
		++RollInputsSent;
		UE_LOG(LogRPGMovementSecurityE2E, Display,
			TEXT("RPG_MOVEMENT_NETTEST ROLL_INPUT Run=%d Action=%s PlayerId=%d"),
			RollInputsSent, *GetNameSafe(RollAction.Get()), TestPlayerId);
	}
	if (Spec->IsActive())
	{
		bRollObservedActive = true;
		UAnimInstance* Anim = Character.GetMesh() ? Character.GetMesh()->GetAnimInstance() : nullptr;
		UAnimMontage* Montage = RollAbilitySystem->GetCurrentMontage();
		if (Anim && Montage && Montage->GetName() == TEXT("AM_Roll") &&
			Anim->Montage_GetPosition(Montage) > 0.02f)
		{
			++RollMontageSamples;
		}
	}
	else if (bRollObservedActive)
	{
		// With high RTT, local prediction may finish before the authority's
		// cancellation arrives. Require playback and inactivity on the client;
		// the server independently verifies its cancellation and budget revoke.
		if (!bRollEnded || RollMontageSamples < 1 || (RollCompletions == 0 && bRollEndedCancelled))
		{
			FailProbe(TEXT("AuthoredRollClientLifetimeFailed"));
			return;
		}
		++RollCompletions;
		UE_LOG(LogRPGMovementSecurityE2E, Display,
			TEXT("RPG_MOVEMENT_NETTEST ROLL_CLIENT_RESULT Run=%d Cancelled=%d MontageSamples=%d PlayerId=%d"),
			RollCompletions, bRollEndedCancelled ? 1 : 0, RollMontageSamples, TestPlayerId);
		bRollObservedActive = bRollEnded = bRollEndedCancelled = false;
		RollMontageSamples = 0;
		if (RollCompletions == 2)
		{
			bClientRollCompleted = true;
			UE_LOG(LogRPGMovementSecurityE2E, Display,
				TEXT("RPG_MOVEMENT_NETTEST ROLL_CLIENT_COMPLETE Runs=2 Natural=1 Inactive=1 PlayerId=%d"), TestPlayerId);
		}
	}
}

void URPGMovementSecurityE2EProbeComponent::TickRollServer(ACharacter& Character, const double Now)
{
	if (FalsePositiveCount != 0 || Now - PhaseStartedAt > 16.0)
	{
		FailProbe(TEXT("AuthoredRollServerViolationOrTimeout"));
		return;
	}
	FGameplayAbilitySpec* Spec = RollAbilitySystem->FindAbilitySpecFromHandle(RollHandle);
	if (!Spec)
	{
		FailProbe(TEXT("AuthoredRollSpecLost"));
		return;
	}
	if (Spec->IsActive())
	{
		if (!bRollObservedActive)
		{
			bRollObservedActive = true;
			RollStartedAt = Now;
			LastServerLocation = Character.GetActorLocation();
		}
		RollDistance += FVector::Dist2D(Character.GetActorLocation(), LastServerLocation);
		LastServerLocation = Character.GetActorLocation();
		bRollObservedAuthorization |= Security->IsMovementAuthorizationActive() &&
			Security->GetAuthorizedMovementReason() == TEXT("PlayerRoll");
		bRollObservedBudgetUse |= bRollObservedAuthorization &&
			Security->GetRemainingAuthorizedMovementDistance() < 550.0f;
		UAnimInstance* Anim = Character.GetMesh() ? Character.GetMesh()->GetAnimInstance() : nullptr;
		UAnimMontage* Montage = RollAbilitySystem->GetCurrentMontage();
		if (Anim && Montage && Montage->GetName() == TEXT("AM_Roll") &&
			Anim->Montage_GetPosition(Montage) > 0.02f && Character.GetMesh()->IsPlayingRootMotion())
		{
			++RollMontageSamples;
		}
		if (RollMontageSamples >= 1 && !ObserveConcurrentRolls())
		{
			return;
		}
		if (RollCompletions == 1 && !bRollCancellationRequested &&
			bRollObservedAuthorization && RollMontageSamples >= 1 && Now - RollStartedAt >= 0.2)
		{
			if (!CancelRollWithPeerIsolation(Now))
			{
				return;
			}
		}
		return;
	}
	if (!bRollObservedActive)
	{
		return;
	}
	RollDistance += FVector::Dist2D(Character.GetActorLocation(), LastServerLocation);
	if (!bRollEnded || bRollEndedCancelled != (RollCompletions == 1) ||
		!bRollObservedAuthorization || RollMontageSamples < 1 || Security->IsMovementAuthorizationActive() ||
		(RollCompletions == 0 && (RollDistance < 100.0 || RollDistance > 700.0)))
	{
		UE_LOG(LogRPGMovementSecurityE2E, Error,
			TEXT("RPG_MOVEMENT_NETTEST ROLL_LIFETIME_DETAIL Run=%d Ended=%d Cancelled=%d Authorized=%d MontageSamples=%d Distance=%.1f ActiveWindow=%d"),
			RollCompletions + 1, bRollEnded ? 1 : 0, bRollEndedCancelled ? 1 : 0,
			bRollObservedAuthorization ? 1 : 0, RollMontageSamples, RollDistance,
			Security->IsMovementAuthorizationActive() ? 1 : 0);
		FailProbe(TEXT("AuthoredRollServerLifetimeFailed"));
		return;
	}
	UE_LOG(LogRPGMovementSecurityE2E, Display,
		TEXT("RPG_MOVEMENT_NETTEST ROLL_SERVER_RESULT Run=%d Cancelled=%d Distance=%.1f MontageSamples=%d BudgetUsed=%d AuthorizationRevoked=1 PlayerId=%d"),
		RollCompletions + 1, bRollEndedCancelled ? 1 : 0, RollDistance, RollMontageSamples,
		bRollObservedBudgetUse ? 1 : 0, TestPlayerId);
	++RollCompletions;
	bRollObservedActive = bRollEnded = bRollEndedCancelled = bRollObservedAuthorization = bRollObservedBudgetUse = false;
	RollDistance = 0.0;
	RollMontageSamples = 0;
	if (RollCompletions == 2)
	{
		UE_LOG(LogRPGMovementSecurityE2E, Display,
			TEXT("RPG_MOVEMENT_NETTEST ROLL_SERVER_COMPLETE Runs=2 Natural=1 Cancelled=1 AuthorizationRevoked=1 PlayerId=%d MaxConcurrent=%d IsolationChecks=%d"),
			TestPlayerId, MaximumConcurrentRollPlayers, PeerIsolationChecks);
		StartPositiveControl(Character, Now);
	}
}

void URPGMovementSecurityE2EProbeComponent::StartPositiveControl(ACharacter& Character, const double Now)
{
	if (bTestAuthorizedMovement &&
		!Security->BeginMovementAuthorization(1.0f, 200.0f, TEXT("BoundedPositiveControl")).IsValid())
	{
		FailProbe(TEXT("PositiveControlAuthorizationFailed"));
		return;
	}
	bPositiveControl = true;
	PositiveControlDeadline = Now + 3.0;
	Character.GetCharacterMovement()->DisableMovement();
	Character.SetActorLocation(Character.GetActorLocation() + FVector(10000.0, 0.0, 0.0),
		false, nullptr, ETeleportType::TeleportPhysics);
}

bool URPGMovementSecurityE2EProbeComponent::PrepareTestAbility(ACharacter& Character)
{
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(&Character);
	if (!ASC)
	{
		FailProbe(TEXT("TestAbilitySystemMissing"));
		return false;
	}
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(
		FGameplayAbilitySpec(URPGMovementAuthorizationTestAbility::StaticClass(), 1, INDEX_NONE, this));
	if (!ASC->TryActivateAbility(Handle))
	{
		FailProbe(TEXT("TestAbilityActivationFailed"));
		return false;
	}
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	TestAbility = Spec ? Cast<URPGMovementAuthorizationTestAbility>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestAbility)
	{
		FailProbe(TEXT("TestAbilityInstanceMissing"));
		return false;
	}
	return true;
}

UAbilityTask_AuthorizedMovement* URPGMovementSecurityE2EProbeComponent::MakeWindow(
	const float Duration, const float Distance, const FName Reason)
{
	FRPGSkillMovementSecurityProfile Profile;
	Profile.bEnabled = true;
	Profile.DurationSeconds = Duration;
	Profile.ExtraDistance = Distance;
	Profile.Reason = Reason;
	UAbilityTask_AuthorizedMovement* Task =
		UAbilityTask_AuthorizedMovement::AuthorizedMovementWindow(TestAbility, Profile);
	Task->OnCancelled.AddDynamic(this, &ThisClass::HandleTaskCancelled);
	Task->OnFinished.AddDynamic(this, &ThisClass::HandleTaskFinished);
	return Task;
}

FGuid URPGMovementSecurityE2EProbeComponent::BeginBlueprintAuthorization(
	URPGSecurityValidationComponent& Component, const float Duration,
	const float Distance, const FName Reason)
{
	struct FParameters
	{
		float DurationSeconds;
		float ExtraDistance;
		FName Reason;
		FGuid ReturnValue;
	} Parameters{ Duration, Distance, Reason, {} };
	UFunction* Function = Component.FindFunction(TEXT("BeginMovementAuthorization"));
	if (!Function || Function->ParmsSize != sizeof(FParameters) ||
		!Function->HasAllFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintAuthorityOnly))
	{
		FailProbe(TEXT("BlueprintAuthorizationFunctionMissing"));
		return {};
	}
	Component.ProcessEvent(Function, &Parameters);
	return Parameters.ReturnValue;
}

void URPGMovementSecurityE2EProbeComponent::TickAuthorizedServer(ACharacter& Character, const double Now)
{
	UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	if (!Movement || FalsePositiveCount != 0)
	{
		FailProbe(TEXT("AuthorizedMovementFalsePositive"));
		return;
	}
	if (Now - PhaseStartedAt > 8.0)
	{
		FailProbe(TEXT("AuthorizedPhaseTimeout"));
		return;
	}
	switch (ServerPhase)
	{
	case EServerPhase::WaitForJump:
		if (Movement->IsFalling())
		{
			++AirborneSamples;
			JumpHeight = FMath::Max(JumpHeight, Character.GetActorLocation().Z - PhaseOrigin.Z);
		}
		else if (AirborneSamples >= 3 && JumpHeight >= 30.0)
		{
			UE_LOG(LogRPGMovementSecurityE2E, Display,
				TEXT("RPG_MOVEMENT_NETTEST JUMP_COMPLETE Samples=%d Height=%.1f"), AirborneSamples, JumpHeight);
			ServerPhase = EServerPhase::Dash;
			PhaseStartedAt = Now;
			DashTask = MakeWindow(1.0f, 900.0f, TEXT("DashProbe"));
			DashTask->OnAuthorized.AddDynamic(this, &ThisClass::HandleDashAuthorized);
			DashTask->ReadyForActivation();
		}
		break;
	case EServerPhase::Dash:
		if (Now - PhaseStartedAt >= 0.35)
		{
			const float Remaining = Security->GetRemainingAuthorizedMovementDistance();
			if (!Security->IsMovementAuthorizationActive() || Remaining <= 0.0f || Remaining >= 900.0f)
			{
				FailProbe(TEXT("DashBudgetNotConsumed"));
				return;
			}
			DashTask->EndTask();
			if (Security->IsMovementAuthorizationActive() || CancelledTasks != 1)
			{
				FailProbe(TEXT("DashCancellationFailed"));
				return;
			}
			UE_LOG(LogRPGMovementSecurityE2E, Display,
				TEXT("RPG_MOVEMENT_NETTEST DASH_COMPLETE Remaining=%.1f Cancelled=1"), Remaining);
			ServerPhase = EServerPhase::Knockback;
			PhaseStartedAt = Now;
			PhaseOrigin = Character.GetActorLocation();
			if (!BeginBlueprintAuthorization(*Security, 0.8f, 600.0f, TEXT("KnockbackProbe")).IsValid())
			{
				FailProbe(TEXT("KnockbackAuthorizationFailed"));
				return;
			}
			Character.LaunchCharacter(FVector(900.0, 0.0, 300.0), true, true);
		}
		break;
	case EServerPhase::Knockback:
		KnockbackDistance = FMath::Max(KnockbackDistance,
			FVector::Dist2D(Character.GetActorLocation(), PhaseOrigin));
		bKnockbackObserved |= Movement->IsFalling() && KnockbackDistance >= 50.0;
		if (Now - PhaseStartedAt >= 1.2 && Movement->IsMovingOnGround())
		{
			if (!bKnockbackObserved || Security->IsMovementAuthorizationActive())
			{
				FailProbe(TEXT("KnockbackOrExpiryNotObserved"));
				return;
			}
			UE_LOG(LogRPGMovementSecurityE2E, Display,
				TEXT("RPG_MOVEMENT_NETTEST KNOCKBACK_COMPLETE Distance=%.1f Expired=1"), KnockbackDistance);
			ServerPhase = EServerPhase::Overlap;
			PhaseStartedAt = Now;
			OlderTask = MakeWindow(1.0f, 400.0f, TEXT("SharedMovementReason"));
			OlderTask->ReadyForActivation();
			NewerTask = MakeWindow(0.5f, 900.0f, TEXT("SharedMovementReason"));
			NewerTask->ReadyForActivation();
			OlderTask->EndTask();
			const FGuid InvalidGrant = Security->BeginMovementAuthorization(-1.0f, 100.0f,
				TEXT("InvalidProfile"));
			Security->EndMovementAuthorization(FGuid::NewGuid(), true);
			if (!Security->IsMovementAuthorizationActive() ||
				Security->GetRemainingAuthorizedMovementDistance() != 900.0f || InvalidGrant.IsValid())
			{
				FailProbe(TEXT("OldTaskRevokedNewerAuthorization"));
			}
		}
		break;
	case EServerPhase::Overlap:
		if (Now - PhaseStartedAt >= 0.7)
		{
			if (Security->IsMovementAuthorizationActive() || FinishedTasks != 1 || CancelledTasks != 2)
			{
				FailProbe(TEXT("OverlapTaskLifetimeFailed"));
				return;
			}
			ImmediateCancelTask = MakeWindow(0.5f, 500.0f, TEXT("ImmediateCancel"));
			ImmediateCancelTask->OnAuthorized.AddDynamic(this, &ThisClass::HandleImmediateCancellation);
			ImmediateCancelTask->ReadyForActivation();
			if (Security->IsMovementAuthorizationActive() || CancelledTasks != 3)
			{
				FailProbe(TEXT("ReentrantCancellationFailed"));
				return;
			}
			UE_LOG(LogRPGMovementSecurityE2E, Display,
				TEXT("RPG_MOVEMENT_NETTEST OVERLAP_COMPLETE SameReason=1 Expired=1 ReentrantCancel=1"));
			StartPositiveControl(Character, Now);
		}
		break;
	default:
		break;
	}
}

void URPGMovementSecurityE2EProbeComponent::HandleDashAuthorized()
{
	ACharacter* Character = ObservedCharacter.Get();
	if (!Character || !Security->IsMovementAuthorizationActive())
	{
		FailProbe(TEXT("DashNotAuthorized"));
		return;
	}
	Character->SetActorLocation(Character->GetActorLocation() + FVector(700.0, 0.0, 0.0),
		false, nullptr, ETeleportType::TeleportPhysics);
}

void URPGMovementSecurityE2EProbeComponent::HandleTaskCancelled() { ++CancelledTasks; }
void URPGMovementSecurityE2EProbeComponent::HandleTaskFinished() { ++FinishedTasks; }
void URPGMovementSecurityE2EProbeComponent::HandleImmediateCancellation() { ImmediateCancelTask->EndTask(); }

void URPGMovementSecurityE2EProbeComponent::HandleViolation(const FRPGSecurityViolation& Violation)
{
	if (Violation.Type != ERPGSecurityViolationType::MovementSpeed &&
		Violation.Type != ERPGSecurityViolationType::MovementDiscontinuity)
	{
		return;
	}
	if (bPositiveControl)
	{
		bPositiveControlObserved |= Violation.Type == ERPGSecurityViolationType::MovementDiscontinuity;
	}
	else
	{
		++FalsePositiveCount;
	}
}

void URPGMovementSecurityE2EProbeComponent::FailProbe(const TCHAR* Reason)
{
	UE_LOG(LogRPGMovementSecurityE2E, Error,
		TEXT("RPG_MOVEMENT_NETTEST FAILED Role=%s Reason=%s PlayerId=%d"),
		GetNetMode() == NM_Client ? TEXT("Client") : TEXT("Server"), Reason, TestPlayerId);
	FinishProbe();
}

void URPGMovementSecurityE2EProbeComponent::FinishProbe()
{
	if (InputSubsystem.IsValid() && MoveAction.IsValid())
	{
		InputSubsystem->StopContinuousInputInjectionForAction(MoveAction.Get());
	}
	if (InputSubsystem.IsValid() && RollAction.IsValid())
	{
		InputSubsystem->StopContinuousInputInjectionForAction(RollAction.Get());
	}
	if (RollAbilitySystem.IsValid())
	{
		RollAbilitySystem->OnAbilityEnded.Remove(RollEndedHandle);
	}
	if (Security.IsValid())
	{
		Security->OnViolationReported.RemoveDynamic(this, &ThisClass::HandleViolation);
	}
	if (TestAbility)
	{
		TestAbility->FinishProbeAbility();
	}
	bFinished = true;
	SetComponentTickEnabled(false);
}
