#include "AI/RPGBossDashComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

namespace
{
	double DashServerNow(const UWorld* World)
	{
		const AGameStateBase* Clock = World ? World->GetGameState() : nullptr;
		return Clock ? Clock->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	}
}

URPGBossDashComponent::URPGBossDashComponent()
{
	SetIsReplicatedByDefault(true);
	bReplicateUsingRegisteredSubObjectList = true;
}

void URPGBossDashComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPGBossDashComponent, State);
}

bool URPGBossDashComponent::BeginDash(const FRPGBossDashState& NewState)
{
	const AActor* Owner = GetOwner();
	const URPGBossAIComponent* Boss = Owner ? Owner->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const FRPGBossState Context = Boss ? Boss->GetBossState() : FRPGBossState();
	const double Now = DashServerNow(GetWorld());
	const double Duration = NewState.EndServerTime - NewState.StartServerTime;
	if (!Owner || !Owner->HasAuthority() || State.bActive || !NewState.bActive || !NewState.ActionTag.IsValid() ||
		NewState.Sequence <= 0 || Context.EncounterState != ERPGBossEncounterState::InCombat ||
		Context.EncounterId != NewState.EncounterId || Context.PhaseIndex != NewState.PhaseIndex ||
		NewState.StartLocation.ContainsNaN() || NewState.EndLocation.ContainsNaN() ||
		!FMath::IsFinite(NewState.HitRadius) || NewState.HitRadius <= 0.f || NewState.HitRadius > 500.f ||
		!FMath::IsFinite(Now) || !FMath::IsFinite(NewState.StartServerTime) || !FMath::IsFinite(NewState.EndServerTime) ||
		Duration < .05 || Duration > 5.0 || NewState.StartServerTime > Now + .01 || NewState.EndServerTime <= Now ||
		FVector::DistSquared(NewState.StartLocation, NewState.EndLocation) > FMath::Square(3000.f)) { return false; }
	State = NewState;
	GetOwner()->ForceNetUpdate();
	return true;
}

bool URPGBossDashComponent::EndDash(int32 EncounterId, int32 Sequence)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !State.bActive || State.EncounterId != EncounterId || State.Sequence != Sequence) { return false; }
	State.bActive = false;
	GetOwner()->ForceNetUpdate();
	return true;
}

bool URPGBossDashComponent::IsDashActive() const
{
	const AActor* Owner = GetOwner();
	const URPGBossAIComponent* Boss = Owner ? Owner->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const FRPGBossState Context = Boss ? Boss->GetBossState() : FRPGBossState();
	const double Now = DashServerNow(GetWorld());
	return State.bActive && GetWorld() && (GetWorld()->GetNetMode() != NM_Client || GetWorld()->GetGameState()) &&
		FMath::IsFinite(Now) && Now >= State.StartServerTime && Now < State.EndServerTime &&
		Context.EncounterState == ERPGBossEncounterState::InCombat && Context.EncounterId == State.EncounterId && Context.PhaseIndex == State.PhaseIndex;
}
