#pragma once

#include "UI/Content/RPGTrainingGuideWidget.h"
#include "RPGIntroApproachGuideWidget.generated.h"

/** Owner-only Intro1 replica display. Shares the existing card/result-control lifetime. */
UCLASS()
class PROJECT_RPG_API URPGIntroApproachGuideWidget final : public URPGTrainingGuideWidget
{
	GENERATED_BODY()
public:
	virtual const TCHAR* GetPracticeContentId() const override;
protected:
	virtual int32 GetGoalCount() const override { return 3; }
	virtual bool HasActiveProgress(const APawn* Pawn) const override;
	virtual void ShowCurrentProgress(const ARPGPlayerController& Controller) override;
private:
	FText ReadStepHint(int32 Step, const ARPGPlayerController& Controller) const;
};
