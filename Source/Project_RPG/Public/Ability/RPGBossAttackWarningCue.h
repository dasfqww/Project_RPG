#pragma once

#include "GameplayCueNotify_Static.h"
#include "RPGBossAttackWarningCue.generated.h"

class UNiagaraSystem;

/** Cosmetic-only raid warning. Dedicated/NullRHI execution never dereferences a failed spawn. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGBossAttackWarningCue : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	URPGBossAttackWarningCue();
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Presentation")
	TObjectPtr<UNiagaraSystem> WarningSystem;
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
};
