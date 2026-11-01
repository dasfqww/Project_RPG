#include "GameState/RPGBossBattleGameState.h"

#include "AI/RPGBossAIComponent.h"
#include "Character/RPGBossCharacter.h"
#include "Net/UnrealNetwork.h"

void ARPGBossBattleGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGBossBattleGameState, BattleSnapshot);
}

float ARPGBossBattleGameState::GetRemainingBattleTime() const
{
	return BattleSnapshot.BattleState == EBossBattleState::InProgress
		? static_cast<float>(FMath::Max(0.0, BattleSnapshot.EndServerTime - GetServerWorldTimeSeconds()))
		: 0.f;
}

void ARPGBossBattleGameState::SetBoss(ARPGBossCharacter* NewBoss)
{
	if (!HasAuthority() || BattleSnapshot.BattleState != EBossBattleState::Waiting ||
		!IsValid(NewBoss) || NewBoss->GetWorld() != GetWorld() || !NewBoss->HasAuthority() ||
		!IsValid(NewBoss->BossAIComponent) || BattleSnapshot.Boss == NewBoss)
	{
		return;
	}
	BattleSnapshot.Boss = NewBoss;
	PublishBattleSnapshot();
}

void ARPGBossBattleGameState::StartBattle(float DurationSeconds)
{
	if (!HasAuthority() || BattleSnapshot.BattleState != EBossBattleState::Waiting ||
		!IsValid(BattleSnapshot.Boss) || BattleSnapshot.Boss->GetWorld() != GetWorld() ||
		!BattleSnapshot.Boss->HasAuthority() || !IsValid(BattleSnapshot.Boss->BossAIComponent) ||
		BattleSnapshot.Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::InCombat ||
		!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.f)
	{
		return;
	}
	BattleSnapshot.EndServerTime = GetServerWorldTimeSeconds() + DurationSeconds;
	BattleSnapshot.BattleState = EBossBattleState::InProgress;
	PublishBattleSnapshot();
}

void ARPGBossBattleGameState::FinishBattle(EBossBattleState Result)
{
	if (!HasAuthority() || BattleSnapshot.BattleState != EBossBattleState::InProgress ||
		(Result != EBossBattleState::Victory && Result != EBossBattleState::Defeated))
	{
		return;
	}
	const ARPGBossCharacter* Boss = BattleSnapshot.Boss;
	if (Result == EBossBattleState::Victory &&
		(!IsValid(Boss) || !IsValid(Boss->BossAIComponent) ||
		Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Dead))
	{
		return;
	}
	if (Result == EBossBattleState::Defeated && IsValid(Boss) && !Boss->IsActorBeingDestroyed() &&
		(!IsValid(Boss->BossAIComponent) ||
		Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Wiped))
	{
		return;
	}
	BattleSnapshot.BattleState = Result;
	PublishBattleSnapshot();
}

void ARPGBossBattleGameState::PrepareNextBattle()
{
	if (!HasAuthority() || (BattleSnapshot.BattleState != EBossBattleState::Victory &&
		BattleSnapshot.BattleState != EBossBattleState::Defeated) ||
		(IsValid(BattleSnapshot.Boss) && !BattleSnapshot.Boss->IsActorBeingDestroyed()))
	{
		return;
	}
	BattleSnapshot.BattleState = EBossBattleState::Waiting;
	BattleSnapshot.Boss = nullptr;
	BattleSnapshot.EndServerTime = 0.0;
	PublishBattleSnapshot();
}

void ARPGBossBattleGameState::PublishBattleSnapshot()
{
	++BattleSnapshot.Revision;
	ForceNetUpdate();
	OnBattleSnapshotChanged.Broadcast(BattleSnapshot);
}

void ARPGBossBattleGameState::OnRep_BattleSnapshot()
{
	OnBattleSnapshotChanged.Broadcast(BattleSnapshot);
}
