#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Type/RPGEnumTypes.h"
#include "RPGPlayerClassDefinition.generated.h"

class URPGSkillDefinition;
class URPGGameplayAbility_SkillContainer;
class URPGGameplayAbility;
class FDataValidationContext;

/** An authored binding, available before grant, registration and cooldown queries. */
USTRUCT(BlueprintType)
struct FRPGPlayerClassSkill
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<URPGSkillDefinition> Definition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<URPGGameplayAbility_SkillContainer> AbilityClass;
};

/** Shared class catalog. Acquisition, grants and the current eight slots live elsewhere. */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGPlayerClassDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxCombatSkills = 8;
	static const FPrimaryAssetType PrimaryAssetType;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	const FRPGPlayerClassSkill* FindSkill(FGameplayTag SkillTag) const;
	bool ValidateConfiguration(FText& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** Stable save identity; independent of display name and asset path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Class")
	FName ClassId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skills")
	TArray<FRPGPlayerClassSkill> Skills;

	/** Empty means no class-wide weapon restriction. Uses the existing equipment identities. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TArray<ERPGGladiatorWeaponType> AllowedWeaponTypes;

	/** Creation recipe only. Empty tags preserve deliberately empty positions; never teach skills. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skills", meta = (EditFixedOrder))
	TArray<FGameplayTag> DefaultSkillSlots;

	/** Optional until the class identity is implemented; independent of the eight combat slots. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	TSubclassOf<URPGGameplayAbility> IdentityAbility;

	/** Optional: event-driven identities do not require a key binding. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (Categories = "InputTag"))
	FGameplayTag IdentityInputTag;
};
