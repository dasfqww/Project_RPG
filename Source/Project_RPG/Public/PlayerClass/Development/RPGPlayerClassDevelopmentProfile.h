#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Type/RPGStructTypes.h"
#include "RPGPlayerClassDevelopmentProfile.generated.h"

class URPGPlayerClassDefinition;
struct FRPGPlayerSkillRestoreData;
class FDataValidationContext;

USTRUCT(BlueprintType)
struct FRPGPlayerClassDevelopmentSkill
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag SkillTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FRPGSkillSaveData SaveData;
};

/** Explicit development input, never a saved-character fallback or an acquisition policy. */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGPlayerClassDevelopmentProfile : public UDataAsset
{
	GENERATED_BODY()
public:
	bool BuildRestoreData(FRPGPlayerSkillRestoreData& OutData, FText& OutError) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Development")
	TObjectPtr<URPGPlayerClassDefinition> ClassDefinition;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Development")
	int32 DataVersion = 1;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Development", meta = (ClampMin = "0"))
	int32 TotalSP = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Development")
	TArray<FRPGPlayerClassDevelopmentSkill> AcquiredSkills;
	/** Includes deliberately empty positions; never filled from class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Development")
	TArray<FGameplayTag> SkillSlots;
};
