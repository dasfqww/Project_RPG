#pragma once
#include "GameplayEffect.h"
#include "RPGEquipmentProgressionEffect.generated.h"

/** Server-owned aggregate gear contribution; removable without changing base stats. */
UCLASS()
class PROJECT_RPG_API URPGEquipmentProgressionEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGEquipmentProgressionEffect();
	static FName AttackMagnitude() { return TEXT("FirstSlice.Equipment.Attack"); }
	static FName HealthMagnitude() { return TEXT("FirstSlice.Equipment.MaximumHealth"); }
	static FName CooldownReductionMagnitude() { return TEXT("FirstSlice.Equipment.CooldownReductionPercent"); }
	static FName DamageReductionMagnitude() { return TEXT("FirstSlice.Equipment.DamageReductionPercent"); }
};
