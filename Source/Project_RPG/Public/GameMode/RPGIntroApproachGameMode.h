#pragma once

#include "CoreMinimal.h"
#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"
#include "GameMode/RPGTrainingGameMode.h"
#include "RPGIntroApproachGameMode.generated.h"

/** Intro1 uses the existing practice roster/lifetime/fresh-membership and settlement gate. */
UCLASS()
class PROJECT_RPG_API ARPGIntroApproachGameMode : public ARPGTrainingGameMode
{
	GENERATED_BODY()
protected:
	virtual const TCHAR* GetPracticeContentId() const override;
	virtual UActorComponent* FindPracticeObjectives(ARPGPlayer* Pawn) const override;
	virtual FPracticeStatus ReadPracticeStatus(const UActorComponent* Objectives) const override;
	virtual void BindPracticeObjectives(UActorComponent* Objectives) override;
	virtual void UnbindPracticeObjectives(UActorComponent* Objectives) override;
private:
	UFUNCTION() void HandleApproachProgressChanged(const FRPGIntroApproachProgress& Progress);
};
