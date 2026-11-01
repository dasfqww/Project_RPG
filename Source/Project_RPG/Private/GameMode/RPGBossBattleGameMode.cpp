// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/RPGBossBattleGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/TargetPoint.h"
#include "AI/RPGBossAIComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "GameState/RPGBossBattleGameState.h"
#include "UI/RPGWidgetBase.h"
#include "Manager/SoundManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Character/RPGPlayer.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGBossMapE2EProbeComponent.h"
#include "Tests/RPGDungeonFlowE2EProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

ARPGBossBattleGameMode::ARPGBossBattleGameMode():
	BattleTimeLimit(30.f),
	DeathCount(3)
{
	GameStateClass = ARPGBossBattleGameState::StaticClass();
	CurrentBossBattleState = EBossBattleState::Waiting;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2E")))
	{
		CreateDefaultSubobject<URPGBossMapE2EProbeComponent>(TEXT("BossMapE2EProbe"));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonFlowE2E")))
	{
		CreateDefaultSubobject<URPGDungeonFlowE2EServer>(TEXT("DungeonFlowE2EProbe"));
	}
#endif
}

void ARPGBossBattleGameMode::BeginPlay()
{
	bBossSpawnEndingPlay = false;
	// Bind before parent startup can confirm the assignment. SpawnBoss below
	// also handles an assignment already confirmed before this binding.
	OnBackendDungeonDifficultyConfirmed.AddUniqueDynamic(
		this, &ThisClass::HandleBackendDungeonDifficultyConfirmed);
	Super::BeginPlay();
	// ReceiveBeginPlay in legacy Blueprint children can write the old
	// InProgress value before the boss/player readiness gate has run. The
	// replicated session is authoritative; never inherit that local shortcut.
	if (const ARPGBossBattleGameState* BattleGameState = GetGameState<ARPGBossBattleGameState>())
	{
		CurrentBossBattleState = BattleGameState->GetBattleSnapshot().BattleState;
	}

	SpawnPoint = UGameplayStatics::GetActorOfClass(GetWorld(), ATargetPoint::StaticClass());
	SpawnBoss();
}

void ARPGBossBattleGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bBossSpawnEndingPlay = true;
	GetWorldTimerManager().ClearTimer(BackendDifficultySpawnTimer);
	OnBackendDungeonDifficultyConfirmed.RemoveDynamic(
		this, &ThisClass::HandleBackendDungeonDifficultyConfirmed);
	Super::EndPlay(EndPlayReason);
}

void ARPGBossBattleGameMode::HandleBackendDungeonDifficultyConfirmed(const ERPGGameDifficulty Difficulty)
{
	UWorld* World = GetWorld();
	if (bBossSpawnEndingPlay || !HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this || !IsBackendDungeonDifficultyReady()
		|| Difficulty != GetCurrentGameDifficulty()) return;
	if (IsValid(BossCharacter) || BackendDifficultySpawnTimer.IsValid()) return;
	// Let all server BP confirmation handlers finish before possession can read
	// the authored boss/startup configuration. Re-check the original world next tick.
	const TWeakObjectPtr<UWorld> ExpectedWorld(World);
	BackendDifficultySpawnTimer = GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this, ExpectedWorld, Difficulty]()
		{
			BackendDifficultySpawnTimer.Invalidate();
			UWorld* CurrentWorld = ExpectedWorld.Get();
			if (bBossSpawnEndingPlay || !HasAuthority() || !IsValid(CurrentWorld) || CurrentWorld != GetWorld()
				|| CurrentWorld->bIsTearingDown || CurrentWorld->GetAuthGameMode() != this
				|| !IsBackendDungeonDifficultyReady() || Difficulty != GetCurrentGameDifficulty()) return;
			SpawnBoss();
		}));
}

void ARPGBossBattleGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (CurrentBossBattleState == EBossBattleState::InProgress)
	{
		// Mechanic deadlines can wipe BossAI while participants are still alive.
		// Resolve the session after the boss-state notification stack has finished,
		// using the same defeat/retry path as battle timeout or party wipe.
		if (IsValid(BossCharacter) && IsValid(BossCharacter->BossAIComponent) &&
			BossCharacter->BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::Wiped)
		{
			Defeated();
			return;
		}
		const ARPGBossBattleGameState* BattleGameState = GetGameState<ARPGBossBattleGameState>();
		if (BattleGameState && BattleGameState->GetRemainingBattleTime() <= 0.f)
		{
			Defeated();
			return;
		}
		RegisterLivingParticipants();
		if (!HasLivingParticipant())
		{
			Defeated();
		}
	}
	else if (CurrentBossBattleState == EBossBattleState::Waiting)
	{
		TryStartBattle();
	}
}

void ARPGBossBattleGameMode::Logout(AController* Exiting)
{
	Participants.Remove(TWeakObjectPtr<APlayerController>(Cast<APlayerController>(Exiting)));
	ParticipantRetryStates.Remove(TWeakObjectPtr<APlayerController>(Cast<APlayerController>(Exiting)));
	Super::Logout(Exiting);
}

void ARPGBossBattleGameMode::RegisterLivingParticipants()
{
	if (!IsValid(SpawnPoint) || !GetWorld())
	{
		return;
	}
	const float RadiusSquared = FMath::Square(ParticipationRadius);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		const ARPGPlayer* PlayerPawn = Controller ? Cast<ARPGPlayer>(Controller->GetPawn()) : nullptr;
		if (!IsValid(PlayerPawn) || IsDungeonGameplayRevoked(Controller) || IsDungeonGameplayRevoked(PlayerPawn) ||
			FVector::DistSquared(PlayerPawn->GetActorLocation(), SpawnPoint->GetActorLocation()) > RadiusSquared)
		{
			continue;
		}
		const UAbilitySystemComponent* ASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn);
		if (ASC && !IsDungeonGameplayRevoked(Controller) && !IsDungeonGameplayRevoked(PlayerPawn) && ASC->GetAvatarActor() == PlayerPawn &&
			ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) &&
			FMath::IsFinite(ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())) &&
			ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) > 0.f &&
			!ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
		{
			const TWeakObjectPtr<APlayerController> Key(Controller);
			if (!Participants.Contains(Key))
			{
				Participants.Add(Key);
				FParticipantRetryState& RetryState = ParticipantRetryStates.Add(Key);
				RetryState.EntryTransform = PlayerPawn->GetActorTransform();
			}
			ParticipantRetryStates.FindChecked(Key).LastPawn = Controller->GetPawn();
		}
	}
}

bool ARPGBossBattleGameMode::HasLivingParticipant() const
{
	for (const TWeakObjectPtr<APlayerController>& Participant : Participants)
	{
		const APawn* PlayerPawn = Participant.IsValid() ? Participant->GetPawn() : nullptr;
		const UAbilitySystemComponent* ASC = IsValid(PlayerPawn)
			? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn) : nullptr;
		if (ASC && !IsDungeonGameplayRevoked(Participant.Get()) && !IsDungeonGameplayRevoked(PlayerPawn) && ASC->GetAvatarActor() == PlayerPawn &&
			ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) &&
			FMath::IsFinite(ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())) &&
			ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) > 0.f &&
			!ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
		{
			return true;
		}
	}
	return false;
}

void ARPGBossBattleGameMode::SpawnBoss()
{
	UWorld* World = GetWorld();
	if (bBossSpawnEndingPlay || !IsValid(World) || World->bIsTearingDown || IsActorBeingDestroyed()) return;
	// Possession immediately snapshots NPC startup ApplyLevel. Do not create
	// an online boss using an offline/default difficulty and try to re-grant later.
	if (IsOnlineDungeonBattle() && (!IsBackendDungeonDifficultyReady() || World->GetAuthGameMode() != this)) return;
	if (!HasAuthority() || BossCharacter || !BossClass || !SpawnPoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("Boss battle requires a BossCharacter subclass and a TargetPoint."));
		return;
	}

	FActorSpawnParameters SpawnParam;
	SpawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	FVector SpawnLocation = SpawnPoint->GetActorLocation();
	FRotator SpawnRotation = SpawnPoint->GetActorRotation();

	BossCharacter = GetWorld()->SpawnActor<ARPGBossCharacter>(BossClass, SpawnLocation, SpawnRotation, SpawnParam);
	if (BossCharacter)
	{
		BossCharacter->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleBossDestroyed);
		BossCharacter->OnNPCStartUpDataReady.AddUniqueDynamic(this, &ThisClass::HandleBossStartUpReady);
		BossCharacter->BossAIComponent->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::HandleBossStateChanged);
		if (ARPGBossBattleGameState* BattleGameState = GetGameState<ARPGBossBattleGameState>())
		{
			BattleGameState->SetBoss(BossCharacter);
		}
		TryStartBattle();
	}
}

void ARPGBossBattleGameMode::HandleBossStartUpReady()
{
	TryStartBattle();
}

void ARPGBossBattleGameMode::TryStartBattle()
{
	if (bBossSpawnEndingPlay || (IsOnlineDungeonBattle() && !IsBackendDungeonDifficultyReady())) return;
	if (!HasAuthority() || !IsValid(BossCharacter) || !BossCharacter->IsNPCStartUpDataReady() ||
		CurrentBossBattleState != EBossBattleState::Waiting)
	{
		return;
	}
	URPGBossAIComponent* BossAI = BossCharacter->BossAIComponent;
	if (!IsValid(BossAI))
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	// Listen hosts can start before the probe's first tick. Prepare its explicit
	// opt-in fixture at the existing readiness boundary, never during combat.
	if (auto* Probe = FindComponentByClass<URPGBossMapE2EProbeComponent>();
		Probe && !Probe->PreparePlayerAttackBossFixture(BossCharacter)) return;
#endif
	RegisterLivingParticipants();
	if (!HasLivingParticipant())
	{
		return;
	}
	if (BossAI->GetBossState().EncounterState == ERPGBossEncounterState::Idle && !BossAI->StartEncounter())
	{
		UE_LOG(LogTemp, Warning, TEXT("Boss encounter could not start; check startup data and phase definition."));
		return;
	}
	if (BossAI->GetBossState().EncounterState != ERPGBossEncounterState::InCombat)
	{
		return;
	}
	if (ARPGBossBattleGameState* BattleGameState = GetGameState<ARPGBossBattleGameState>())
	{
		BattleGameState->StartBattle(BattleTimeLimit);
		if (BattleGameState->GetBattleSnapshot().BattleState == EBossBattleState::InProgress)
		{
			CurrentBossBattleState = EBossBattleState::InProgress;
			OnBattleStateChanged.Broadcast(CurrentBossBattleState);
		}
	}
}

void ARPGBossBattleGameMode::HandleBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& NewState)
{
	if (NewState.EncounterState == ERPGBossEncounterState::Dead)
	{
		Victory(BossCharacter);
	}
}

void ARPGBossBattleGameMode::HandleBossDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == BossCharacter)
	{
		// Destroying a live boss is not a kill; never grant victory for it.
		Defeated();
	}
}


void ARPGBossBattleGameMode::Victory(AActor* InActor)
{
	if (HasAuthority() && InActor == BossCharacter && IsValid(BossCharacter) &&
		BossCharacter->BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::Dead)
	{
		SetBattleState(EBossBattleState::Victory);
	}
}

void ARPGBossBattleGameMode::Defeated()
{
	SetBattleState(EBossBattleState::Defeated);
}

bool ARPGBossBattleGameMode::TryRestartBattleForParticipant(APlayerController* Requester, const int32 ExpectedBattleRevision)
{
	const ARPGBossBattleGameState* BattleGameState = GetGameState<ARPGBossBattleGameState>();
	if (!HasAuthority() || !IsValid(Requester) || IsDungeonGameplayRevoked(Requester) || IsDungeonGameplayRevoked(Requester->GetPawn()) ||
		(ExpectedBattleRevision != INDEX_NONE && (ExpectedBattleRevision < 0 || !BattleGameState ||
		 BattleGameState->GetBattleSnapshot().Revision != ExpectedBattleRevision)) ||
		(CurrentBossBattleState != EBossBattleState::Victory &&
		 CurrentBossBattleState != EBossBattleState::Defeated) ||
		!Participants.Contains(TWeakObjectPtr<APlayerController>(Requester)))
	{
		return false;
	}
	RestartBattle();
	return CurrentBossBattleState == EBossBattleState::Waiting ||
		CurrentBossBattleState == EBossBattleState::InProgress;
}

bool ARPGBossBattleGameMode::IsOnlineDungeonBattle() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UHttpWebManager* WebManager = GameInstance ? GameInstance->GetSubsystem<UHttpWebManager>() : nullptr;
	return WebManager && WebManager->IsConfiguredForDungeonServer();
}

void ARPGBossBattleGameMode::RestartBattle()
{
	if (!HasAuthority() || bRestartingBattle || (CurrentBossBattleState != EBossBattleState::Victory &&
		CurrentBossBattleState != EBossBattleState::Defeated) ||
		(CurrentBossBattleState == EBossBattleState::Victory && IsOnlineDungeonBattle()))
	{
		return;
	}
	ARPGBossBattleGameState* BattleGameState = GetGameState<ARPGBossBattleGameState>();
	if (!BattleGameState || BattleGameState->GetBattleSnapshot().BattleState != CurrentBossBattleState)
	{
		return;
	}
	TGuardValue<bool> RestartGuard(bRestartingBattle, true);
	// Restore a saved custom/flying mode before the health reset's MOVE_None
	// fallback, so an existing participant keeps its original movement mode.
	RestoreParticipantMovement();
	// Keep the result and participant authorization if recovery fails. A retry
	// can then be requested again; never start combat with missing player pawns.
	if (!ResetParticipantsForRetry())
	{
		UE_LOG(LogTemp, Warning, TEXT("Boss retry is waiting for participant pawn recovery."));
		return;
	}
	if (IsValid(BossCharacter))
	{
		BossCharacter->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleBossDestroyed);
		BossCharacter->OnNPCStartUpDataReady.RemoveDynamic(this, &ThisClass::HandleBossStartUpReady);
		if (URPGBossAIComponent* BossAI = BossCharacter->BossAIComponent)
		{
			BossAI->OnBossStateChanged.RemoveDynamic(this, &ThisClass::HandleBossStateChanged);
			BossAI->BeginReset();
		}
		BossCharacter->Destroy();
	}
	BossCharacter = nullptr;
	BattleGameState->PrepareNextBattle();
	if (BattleGameState->GetBattleSnapshot().BattleState != EBossBattleState::Waiting)
	{
		UE_LOG(LogTemp, Error, TEXT("Boss battle could not prepare a new encounter."));
		return;
	}
	CurrentBossBattleState = EBossBattleState::Waiting;
	Participants.Reset();
	ParticipantRetryStates.Reset();
	OnBattleStateChanged.Broadcast(CurrentBossBattleState);
	SpawnBoss();
}

void ARPGBossBattleGameMode::DisplayResultWidget(TSubclassOf<URPGWidgetBase> InWidgetClass, USoundBase* InSound)
{
	// Kept for existing Blueprint calls. Widgets are now owned by each local PlayerController.
	(void)InWidgetClass;
	APlayerController* LocalController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!LocalController || !LocalController->IsLocalController() || !InSound)
	{
		return;
	}
	USoundManager::Get()->Play(InSound);
}

void ARPGBossBattleGameMode::SetBattleState(EBossBattleState InState)
{
	if (!HasAuthority() || CurrentBossBattleState != EBossBattleState::InProgress ||
		(InState != EBossBattleState::Victory && InState != EBossBattleState::Defeated))
	{
		return;
	}
	if (InState == EBossBattleState::Victory &&
		(!IsValid(BossCharacter) || BossCharacter->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Dead))
	{
		return;
	}
	if (InState == EBossBattleState::Defeated && IsValid(BossCharacter))
	{
		BossCharacter->BossAIComponent->MarkWiped();
	}
	if (ARPGBossBattleGameState* BattleGameState = GetGameState<ARPGBossBattleGameState>())
	{
		BattleGameState->FinishBattle(InState);
		if (BattleGameState->GetBattleSnapshot().BattleState == InState)
		{
			CurrentBossBattleState = InState;
			if (InState == EBossBattleState::Victory)
			{
				// The native Dead -> Victory path owns party-wide persistence. Legacy
				// Blueprint reward calls remain harmless through the base idempotency guard.
				ReportConfiguredDungeonClear();
			}
			OnBattleStateChanged.Broadcast(CurrentBossBattleState);
		}
	}
}

void ARPGBossBattleGameMode::DisablePlayerMovement()
{
	if (!HasAuthority()) { return; }
	for (const TWeakObjectPtr<APlayerController>& Participant : Participants)
	{
		ACharacter* Character = Participant.IsValid() ? Participant->GetCharacter() : nullptr;
		UCharacterMovementComponent* Movement = IsValid(Character) ? Character->GetCharacterMovement() : nullptr;
		if (Movement && Movement->MovementMode != MOVE_None)
		{
			FDisabledParticipantMovement& Saved = DisabledParticipantMovement.AddDefaulted_GetRef();
			Saved.Controller = Participant;
			Saved.Character = Character;
			Saved.PreviousMode = Movement->MovementMode;
			Saved.PreviousCustomMode = Movement->CustomMovementMode;
			Movement->DisableMovement();
		}
	}
}

void ARPGBossBattleGameMode::RestoreParticipantMovement()
{
	for (const FDisabledParticipantMovement& Saved : DisabledParticipantMovement)
	{
		ACharacter* Character = Saved.Character.Get();
		if (!Saved.Controller.IsValid() || IsDungeonGameplayRevoked(Saved.Controller.Get())
			|| IsDungeonGameplayRevoked(Character) || Saved.Controller->GetCharacter() != Character)
		{
			continue;
		}
		UCharacterMovementComponent* Movement = IsValid(Character) ? Character->GetCharacterMovement() : nullptr;
		if (Movement && Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(Saved.PreviousMode, Saved.PreviousCustomMode);
		}
	}
	DisabledParticipantMovement.Reset();
}

bool ARPGBossBattleGameMode::ResetParticipantsForRetry()
{
	if (!HasAuthority()) { return false; }
	bool bAllRecovered = true;
	// Possession/spawn callbacks can disconnect a player and mutate Participants.
	const TArray<TWeakObjectPtr<APlayerController>> RetryParticipants = Participants;
	for (const TWeakObjectPtr<APlayerController>& Participant : RetryParticipants)
	{
		APlayerController* Controller = Participant.Get();
		if (!IsValid(Controller) || IsDungeonGameplayRevoked(Controller) || IsDungeonGameplayRevoked(Controller->GetPawn())
			|| Controller->GetWorld() != GetWorld() || !Participants.Contains(Participant))
		{
			continue;
		}
		if (!IsValid(Controller->GetPawn()))
		{
			const FParticipantRetryState* RetryState = ParticipantRetryStates.Find(Participant);
			if (!RetryState) { bAllRecovered = false; continue; }
			const FTransform EntryTransform = RetryState->EntryTransform;
			// Do not repossess an old pawn: RPGPlayer possession would grant its
			// startup abilities/effects again. Retire only our unpossessed orphan.
			APawn* OldPawn = RetryState->LastPawn.Get();
			if (IsValid(OldPawn) && !OldPawn->GetController()) { OldPawn->Destroy(); }
			RestartPlayerAtTransform(Controller, EntryTransform);
		}
		if (!Participant.IsValid() || !Participants.Contains(Participant)
			|| IsDungeonGameplayRevoked(Controller) || IsDungeonGameplayRevoked(Controller->GetPawn())) { continue; }
		ARPGPlayer* PlayerPawn = Cast<ARPGPlayer>(Controller->GetPawn());
		UAbilitySystemComponent* ASC = IsValid(PlayerPawn)
			? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn) : nullptr;
		if (!ASC || ASC->GetAvatarActor() != PlayerPawn ||
			!ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) ||
			!ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute()))
		{
			bAllRecovered = false;
			continue;
		}
		const float MaxHealth = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
		if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.f)
		{
			bAllRecovered = false;
			continue;
		}
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), MaxHealth);
		ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		if (ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) ||
			ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) <= 0.f)
		{
			bAllRecovered = false;
			continue;
		}
		if (!PlayerPawn->RestoreEncounterPresentation())
		{
			bAllRecovered = false;
			continue;
		}
		if (UCharacterMovementComponent* Movement = PlayerPawn->GetCharacterMovement())
		{
			if (Movement->MovementMode == MOVE_None)
			{
				Movement->SetMovementMode(MOVE_Walking);
			}
		}
	}
	return bAllRecovered;
}
