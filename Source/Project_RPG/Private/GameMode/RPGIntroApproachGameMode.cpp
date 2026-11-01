#include "GameMode/RPGIntroApproachGameMode.h"

#include "Character/RPGPlayer.h"

const TCHAR* ARPGIntroApproachGameMode::GetPracticeContentId() const { return TEXT("Progression.Intro1"); }

UActorComponent* ARPGIntroApproachGameMode::FindPracticeObjectives(ARPGPlayer* Pawn) const
{
	return IsValid(Pawn) ? Pawn->FindComponentByClass<URPGIntroApproachObjectiveComponent>() : nullptr;
}

ARPGTrainingGameMode::FPracticeStatus ARPGIntroApproachGameMode::ReadPracticeStatus(const UActorComponent* Objectives) const
{
	const auto* Typed = Cast<URPGIntroApproachObjectiveComponent>(Objectives);
	if (!IsValid(Typed)) return {};
	const auto Progress = Typed->GetApproachProgress();
	return {Progress.bActive, Progress.bActive && !Progress.bStartReached
		&& !Progress.bApproachCompleted && !Progress.bAttackCompleted, Progress.IsComplete()};
}

void ARPGIntroApproachGameMode::BindPracticeObjectives(UActorComponent* Objectives)
{
	if (auto* Typed = Cast<URPGIntroApproachObjectiveComponent>(Objectives))
		Typed->OnApproachProgressChanged.AddUniqueDynamic(this, &ThisClass::HandleApproachProgressChanged);
}

void ARPGIntroApproachGameMode::UnbindPracticeObjectives(UActorComponent* Objectives)
{
	if (auto* Typed = Cast<URPGIntroApproachObjectiveComponent>(Objectives))
		Typed->OnApproachProgressChanged.RemoveDynamic(this, &ThisClass::HandleApproachProgressChanged);
}

void ARPGIntroApproachGameMode::HandleApproachProgressChanged(const FRPGIntroApproachProgress&)
{
	NotifyPracticeProgressChanged();
}
