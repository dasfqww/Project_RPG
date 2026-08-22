#pragma once

#include "Engine/DataAsset.h"
#include "RPGGameData.generated.h"

class UGameplayEffect;

/** Global gameplay-effect references shared by experiences. */
UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG Game Data", ShortTooltip = "Data asset containing global game data."))
class PROJECT_RPG_API URPGGameData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default Gameplay Effects", meta = (DisplayName = "Damage Gameplay Effect (SetByCaller)"))
	TSoftClassPtr<UGameplayEffect> DamageGameplayEffect_SetByCaller;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default Gameplay Effects", meta = (DisplayName = "Incoming Damage Gameplay Effect (SetByCaller)"))
	TSoftClassPtr<UGameplayEffect> IncomingDamageGameplayEffect_SetByCaller;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default Gameplay Effects", meta = (DisplayName = "Heal Gameplay Effect (SetByCaller)"))
	TSoftClassPtr<UGameplayEffect> HealGameplayEffect_SetByCaller;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default Gameplay Effects")
	TSoftClassPtr<UGameplayEffect> DynamicTagGameplayEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default Gameplay Effects")
	TSoftClassPtr<UGameplayEffect> AttributeModifierGameplayEffect;
};
