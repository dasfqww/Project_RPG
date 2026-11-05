#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGBossPatternDefinition.generated.h"

class URPGBossGameplayAbility;

UENUM(BlueprintType)
enum class ERPGBossTelegraphPolicy : uint8 { Inherit, Show, Hide };

UENUM(BlueprintType)
enum class ERPGBossPatternSelection : uint8 { Priority, Random, Sequence };

/** Extend with typed mechanic settings; each ability explicitly validates the types it supports. */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class PROJECT_RPG_API URPGBossPatternSettings : public UObject
{
	GENERATED_BODY()
};

/** An entry identity is independent of its reusable GA class and presentation ActionTag. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossPatternDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss") FGameplayTag PatternId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss") TSubclassOf<URPGBossGameplayAbility> AbilityClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss") ERPGBossTelegraphPolicy TelegraphPolicy = ERPGBossTelegraphPolicy::Inherit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Boss") TObjectPtr<URPGBossPatternSettings> Settings;

	bool IsConfigured() const;
	bool Validate(FText& OutError) const;
	bool ShouldShowTelegraph(bool bDefault) const;
};

/** Server activation snapshot. Never a mutable pointer back into an authored asset. */
USTRUCT()
struct PROJECT_RPG_API FRPGBossPatternExecution
{
	GENERATED_BODY()
	UPROPERTY() FGameplayTag PatternId;
	UPROPERTY() bool bShowTelegraph = true;
	UPROPERTY() TObjectPtr<URPGBossPatternSettings> Settings;
};
