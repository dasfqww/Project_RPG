#include "Character/RPGBossCharacter.h"
#include "Component/Combat/RPGCombatControlComponent.h"

#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AI/RPGBossDashComponent.h"
#include "AI/RPGBossSummonComponent.h"
#include "AI/RPGBossSummonObjectiveComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ARPGBossCharacter::ARPGBossCharacter()
{
	GetCombatControlComponent()->SetDefaultStunImmunity(true);
	GetCombatControlComponent()->SetDefaultKnockbackImmunity(true);
	GetCombatControlComponent()->SetDefaultAirborneImmunity(true);
	GetCombatControlComponent()->SetDefaultSlowImmunity(true);
	GetCombatControlComponent()->SetDefaultHitReactionImmunity(true);
	bReplicateUsingRegisteredSubObjectList = true;
	RPGAbilitySystemComponent->SetIsReplicated(true);
	RPGAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	BossAIComponent = CreateDefaultSubobject<URPGBossAIComponent>(TEXT("BossAIComponent"));
	BossThreatComponent = CreateDefaultSubobject<URPGBossThreatComponent>(TEXT("BossThreatComponent"));
	BossTelegraphPresentation = CreateDefaultSubobject<URPGBossTelegraphPresentationComponent>(TEXT("BossTelegraphPresentation"));
	BossDashComponent = CreateDefaultSubobject<URPGBossDashComponent>(TEXT("BossDashComponent"));
	BossSummonComponent = CreateDefaultSubobject<URPGBossSummonComponent>(TEXT("BossSummonComponent"));
	BossSummonObjectiveComponent = CreateDefaultSubobject<URPGBossSummonObjectiveComponent>(TEXT("BossSummonObjectiveComponent"));
}

void ARPGBossCharacter::BeginPlay()
{
	BossAIComponent->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::HandleBossStateChanged);
	Super::BeginPlay();
	// NPCs normally do not have a PlayerState, so OnRep_PlayerState is not a client initialization hook.
	RPGAbilitySystemComponent->InitAbilityActorInfo(this, this);
	TryStartInitialEncounter();
	UpdateEncounterMovement();
}

void ARPGBossCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	TryStartInitialEncounter();
	UpdateEncounterMovement();
}

void ARPGBossCharacter::HandleBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current)
{
	UpdateEncounterMovement();
}

void ARPGBossCharacter::UpdateEncounterMovement()
{
	if (!HasAuthority()) { return; }
	const FRPGBossState Context = BossAIComponent->GetBossState();
	const ERPGBossEncounterState State = Context.EncounterState;
	const FRPGBossPhaseDefinition* Phase = BossAIComponent->GetActivePhaseDefinition();
	const bool bPhaseSuspended = Phase && Phase->bSuspendCombatAI;
	if (State == ERPGBossEncounterState::Dead)
	{
		StoppedEncounterBrain.Reset();
		bStoppedEncounterMovement = false;
		StopDeadBoss();
		return;
	}
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	AAIController* AI = Cast<AAIController>(GetController());
	if (State == ERPGBossEncounterState::Wiped || State == ERPGBossEncounterState::Resetting || bPhaseSuspended)
	{
		Movement->StopMovementImmediately();
		if (Movement->MovementMode != MOVE_None)
		{
			if (!bStoppedEncounterMovement)
			{
				SavedEncounterMovementMode = Movement->MovementMode;
				SavedEncounterCustomMovementMode = Movement->CustomMovementMode;
				bStoppedEncounterMovement = true;
			}
			Movement->DisableMovement();
		}
		if (AI)
		{
			UBrainComponent* Brain = AI->GetBrainComponent();
			const bool bStopBrain = Brain && Brain->IsRunning() && !Brain->IsPaused();
			if (bStopBrain) { StoppedEncounterBrain = Brain; }
			AI->StopMovement();
			if (!IsValid(this) || !IsValid(AI) || GetController() != AI || BossAIComponent->GetBossState().Revision != Context.Revision) { return; }
			AI->ClearFocus(EAIFocusPriority::Gameplay);
			if (bStopBrain && IsValid(Brain) && AI->GetBrainComponent() == Brain && !Brain->IsPaused())
			{
				if (UBehaviorTreeComponent* Tree = Cast<UBehaviorTreeComponent>(Brain))
				{
					Tree->StopTree(EBTStopMode::Forced);
				}
				else { Brain->StopLogic(TEXT("Boss combat AI suspended by phase/wipe/reset")); }
			}
		}
		return;
	}
	// Cleanup alone must not restart the BT. Restore owned stops only in nonsuspended combat.
	if (State != ERPGBossEncounterState::InCombat) { return; }
	if (bStoppedEncounterMovement)
	{
		bStoppedEncounterMovement = false;
		if (Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(static_cast<EMovementMode>(SavedEncounterMovementMode), SavedEncounterCustomMovementMode);
		}
	}
	UBrainComponent* PreviousBrain = StoppedEncounterBrain.Get();
	StoppedEncounterBrain.Reset();
	// Do not wake a different controller or override another owner's pause/restart.
	if (PreviousBrain && AI && AI->GetBrainComponent() == PreviousBrain && !PreviousBrain->IsPaused() && !PreviousBrain->IsRunning())
	{
		PreviousBrain->RestartLogic();
	}
}

void ARPGBossCharacter::StopDeadBoss()
{
	if (!HasAuthority() || BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Dead) return;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
		AI->ClearFocus(EAIFocusPriority::Gameplay);
		if (UBrainComponent* Brain = AI->GetBrainComponent()) Brain->StopLogic(TEXT("Boss encounter ended in death"));
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	// Do not CancelAllAbilities: the authored GAS death presentation must continue.
}

void ARPGBossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BossAIComponent->OnBossStateChanged.RemoveDynamic(this, &ThisClass::HandleBossStateChanged);
	Super::EndPlay(EndPlayReason);
}

void ARPGBossCharacter::HandleNPCStartUpDataReady()
{
	Super::HandleNPCStartUpDataReady();
	TryStartInitialEncounter();
}

void ARPGBossCharacter::UnPossessed()
{
	StoppedEncounterBrain.Reset();
	BossThreatComponent->ClearThreat();
	Super::UnPossessed();
}

void ARPGBossCharacter::TryStartInitialEncounter()
{
	if (bStartEncounterOnReady && HasActorBegunPlay() && IsNPCStartUpDataReady() &&
		IsValid(GetController()) && BossAIComponent->GetBossState().EncounterId == 0)
	{
		BossAIComponent->StartEncounter();
	}
}
