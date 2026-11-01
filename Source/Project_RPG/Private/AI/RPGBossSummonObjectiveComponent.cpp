#include "AI/RPGBossSummonObjectiveComponent.h"

#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossSummonComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

URPGBossSummonObjectiveComponent::URPGBossSummonObjectiveComponent()
{
	SetIsReplicatedByDefault(true);
	bReplicateUsingRegisteredSubObjectList = true;
	PrimaryComponentTick.bCanEverTick = false;
}

void URPGBossSummonObjectiveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPGBossSummonObjectiveComponent, Progress);
}

void URPGBossSummonObjectiveComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority()) { return; }
	BoundBoss = GetOwner()->FindComponentByClass<URPGBossAIComponent>();
	BoundSummons = GetOwner()->FindComponentByClass<URPGBossSummonComponent>();
	if (BoundBoss.IsValid()) { BoundBoss->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::OnBossStateChanged); }
	if (BoundSummons.IsValid())
	{
		BoundSummons->OnWaveSpawned.AddUniqueDynamic(this, &ThisClass::OnWaveSpawned);
		BoundSummons->OnSummonDefeated.AddUniqueDynamic(this, &ThisClass::OnSummonDefeated);
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner())) { Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnSourceControllerChanged); }
	SynchronizePhase();
}

bool URPGBossSummonObjectiveComponent::IsContextCurrent() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->HasAuthority() || !Progress.bActive ||
		!SourceController.IsValid() || Pawn->GetController() != SourceController.Get() ||
		!BoundBoss.IsValid() || !BoundSummons.IsValid() ||
		Pawn->FindComponentByClass<URPGBossAIComponent>() != BoundBoss.Get() ||
		Pawn->FindComponentByClass<URPGBossSummonComponent>() != BoundSummons.Get()) { return false; }
	const FRPGBossState State = BoundBoss->GetBossState();
	return State.EncounterState == ERPGBossEncounterState::InCombat &&
		State.EncounterId == Progress.EncounterId && State.PhaseIndex == Progress.PhaseIndex;
}

void URPGBossSummonObjectiveComponent::CancelObjective()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CompletionTimer); }
	Progress.bActive = false;
	Progress.Sequence = 0;
	Progress.RequiredCount = 0;
	Progress.DefeatedCount = 0;
	CompletionPhase = FGameplayTag();
	DefeatedSlots.Reset();
	SourceController.Reset();
}

void URPGBossSummonObjectiveComponent::SynchronizePhase()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !BoundBoss.IsValid()) { return; }
	const FRPGBossState State = BoundBoss->GetBossState();
	// Enrage/other updates do not restart progress. A controller change cancels
	// this phase's objective permanently; a replacement cannot claim its old kills.
	if (State.EncounterState == ERPGBossEncounterState::InCombat &&
		State.EncounterId == Progress.EncounterId && State.PhaseIndex == Progress.PhaseIndex) { return; }
	const bool bWasActive = Progress.bActive;
	CancelObjective();
	Progress.EncounterId = State.EncounterId;
	Progress.PhaseIndex = State.PhaseIndex;
	const FRPGBossPhaseDefinition* Phase = BoundBoss->GetActivePhaseDefinition();
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (Phase && Phase->SummonWaveCompletionPhase.IsValid() && BoundSummons.IsValid() && Pawn && IsValid(Pawn->GetController()))
	{
		CompletionPhase = Phase->SummonWaveCompletionPhase;
		SourceController = Pawn->GetController();
		Progress.bActive = true;
	}
	if (bWasActive || Progress.bActive) { Publish(); }
}

void URPGBossSummonObjectiveComponent::OnWaveSpawned(int32 EncounterId, int32 PhaseIndex, int32 Sequence, int32 MemberCount)
{
	SynchronizePhase();
	if (!IsContextCurrent() || Progress.Sequence != 0 || Sequence <= 0 || MemberCount < 1 || MemberCount > 8 ||
		Progress.EncounterId != EncounterId || Progress.PhaseIndex != PhaseIndex) { return; }
	Progress.Sequence = Sequence;
	Progress.RequiredCount = MemberCount;
	Publish();
}

void URPGBossSummonObjectiveComponent::OnSummonDefeated(int32 EncounterId, int32 PhaseIndex, int32 Sequence, int32 Slot)
{
	if (!IsContextCurrent() || Progress.EncounterId != EncounterId || Progress.PhaseIndex != PhaseIndex ||
		Progress.Sequence <= 0 || Progress.Sequence != Sequence || Slot < 0 || Slot >= Progress.RequiredCount || DefeatedSlots.Contains(Slot)) { return; }
	DefeatedSlots.Add(Slot);
	Progress.DefeatedCount = DefeatedSlots.Num();
	if (Progress.DefeatedCount == Progress.RequiredCount) { ScheduleCompletion(); }
	Publish();
}

void URPGBossSummonObjectiveComponent::ScheduleCompletion(bool bRetry)
{
	if (!GetWorld()) { return; }
	const int32 Encounter = Progress.EncounterId;
	const int32 Phase = Progress.PhaseIndex;
	const int32 Wave = Progress.Sequence;
	const FTimerDelegate Callback = FTimerDelegate::CreateWeakLambda(this,
		[this, Encounter, Phase, Wave]() { TryComplete(Encounter, Phase, Wave); });
	if (bRetry) { GetWorld()->GetTimerManager().SetTimer(CompletionTimer, Callback, .1f, false); }
	else { CompletionTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(Callback); }
}

void URPGBossSummonObjectiveComponent::TryComplete(int32 EncounterId, int32 PhaseIndex, int32 Sequence)
{
	if (!IsContextCurrent() || Progress.EncounterId != EncounterId || Progress.PhaseIndex != PhaseIndex ||
		Progress.Sequence != Sequence || Progress.RequiredCount <= 0 || Progress.DefeatedCount != Progress.RequiredCount) { return; }
	// Leave the GAS HP notification / summon broadcast stack before advancing.
	// BossAI independently checks health and the phase deadline; a delayed callback
	// cannot turn an expired phase into success. Transient mutation guards may retry.
	const FGameplayTag CurrentPhase = BoundBoss->GetBossState().PhaseTag;
	if (!BoundBoss->AdvanceToPhase(EncounterId, CurrentPhase, CompletionPhase) && IsContextCurrent()) { ScheduleCompletion(true); }
}

void URPGBossSummonObjectiveComponent::Publish()
{
	GetOwner()->FlushNetDormancy();
	GetOwner()->ForceNetUpdate();
	OnObjectiveChanged.Broadcast(Progress);
}

void URPGBossSummonObjectiveComponent::OnRep_Progress()
{
	OnObjectiveChanged.Broadcast(Progress);
}

void URPGBossSummonObjectiveComponent::OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current)
{
	SynchronizePhase();
}

void URPGBossSummonObjectiveComponent::OnSourceControllerChanged(APawn* Pawn, AController* Previous, AController* Current)
{
	if (Previous != Current && Progress.bActive) { CancelObjective(); Publish(); }
}

void URPGBossSummonObjectiveComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelObjective();
	if (BoundBoss.IsValid()) { BoundBoss->OnBossStateChanged.RemoveDynamic(this, &ThisClass::OnBossStateChanged); }
	if (BoundSummons.IsValid())
	{
		BoundSummons->OnWaveSpawned.RemoveDynamic(this, &ThisClass::OnWaveSpawned);
		BoundSummons->OnSummonDefeated.RemoveDynamic(this, &ThisClass::OnSummonDefeated);
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner())) { Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnSourceControllerChanged); }
	Super::EndPlay(Reason);
}
