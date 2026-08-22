#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "AttributeSet.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "RPGAbilitySet.generated.h"

class UGameplayEffect;
class URPGAbilitySystemComponent;
class URPGGameplayAbility;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAbilitySet_GameplayAbility
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<URPGGameplayAbility> Ability;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 AbilityLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAbilitySet_GameplayEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayEffect> GameplayEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0"))
	float EffectLevel = 1.0f;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAbilitySet_AttributeSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UAttributeSet> AttributeSet;
};

/** Handles all runtime objects granted by one ability set. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAbilitySet_GrantedHandles
{
	GENERATED_BODY()

public:
	void AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle);
	void AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle);
	void AddAttributeSet(UAttributeSet* AttributeSet);
	void TakeFromAbilitySystem(URPGAbilitySystemComponent* AbilitySystemComponent);

private:
	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

	UPROPERTY()
	TArray<FActiveGameplayEffectHandle> GameplayEffectHandles;

	UPROPERTY()
	TArray<TObjectPtr<UAttributeSet>> GrantedAttributeSets;
};

/** Immutable collection of abilities, effects, and attributes granted as one unit. */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	void GiveToAbilitySystem(
		URPGAbilitySystemComponent* AbilitySystemComponent,
		FRPGAbilitySet_GrantedHandles* OutGrantedHandles = nullptr,
		UObject* SourceObject = nullptr) const;

	const TArray<FRPGAbilitySet_GameplayAbility>& GetGrantedGameplayAbilities() const
	{
		return GrantedGameplayAbilities;
	}

	const TArray<FRPGAbilitySet_GameplayEffect>& GetGrantedGameplayEffects() const
	{
		return GrantedGameplayEffects;
	}

	const TArray<FRPGAbilitySet_AttributeSet>& GetGrantedAttributes() const
	{
		return GrantedAttributes;
	}

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Abilities", meta = (TitleProperty = "Ability"))
	TArray<FRPGAbilitySet_GameplayAbility> GrantedGameplayAbilities;

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effects", meta = (TitleProperty = "GameplayEffect"))
	TArray<FRPGAbilitySet_GameplayEffect> GrantedGameplayEffects;

	UPROPERTY(EditDefaultsOnly, Category = "Attribute Sets", meta = (TitleProperty = "AttributeSet"))
	TArray<FRPGAbilitySet_AttributeSet> GrantedAttributes;
};
