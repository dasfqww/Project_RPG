// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "RPGAttributeSet.generated.h"

class IPawnUIInterface;

#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	URPGAttributeSet();

	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackSpeed, Category = "Speed")
	FGameplayAttributeData AttackSpeed;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, AttackSpeed);

	UFUNCTION()
	void OnRep_AttackSpeed(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeed, Category = "Speed")
	FGameplayAttributeData MoveSpeed;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, MoveSpeed);

	UFUNCTION()
	void OnRep_MoveSpeed(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentHealth, Category = "Health")
		FGameplayAttributeData CurrentHealth;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, CurrentHealth);

	UFUNCTION()
	void OnRep_CurrentHealth(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Health")
		FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, MaxHealth);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentMana, Category = "Mana")
	FGameplayAttributeData CurrentMana;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, CurrentMana);

	UFUNCTION()
	void OnRep_CurrentMana(const FGameplayAttributeData& OldValue);
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxMana, Category = "Mana")
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, MaxMana);

	UFUNCTION()
	void OnRep_MaxMana(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentStamina, Category = "Stamina")
	FGameplayAttributeData CurrentStamina;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, CurrentStamina);

	UFUNCTION()
	void OnRep_CurrentStamina(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxStamina, Category = "Stamina")
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, MaxStamina);

	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentRage, Category = "Rage")
		FGameplayAttributeData CurrentRage;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, CurrentRage);

	UFUNCTION()
	void OnRep_CurrentRage(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxRage, Category = "Rage")
		FGameplayAttributeData MaxRage;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, MaxRage);

	UFUNCTION()
	void OnRep_MaxRage(const FGameplayAttributeData& OldValue);

	// Identity Gauge Attributes
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentIdentityGauge, Category = "Identity")
	FGameplayAttributeData CurrentIdentityGauge;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, CurrentIdentityGauge);

	UFUNCTION()
	void OnRep_CurrentIdentityGauge(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxIdentityGauge, Category = "Identity")
	FGameplayAttributeData MaxIdentityGauge;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, MaxIdentityGauge);

	UFUNCTION()
	void OnRep_MaxIdentityGauge(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IdentityGainMultiplier, Category = "Identity")
	FGameplayAttributeData IdentityGainMultiplier;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, IdentityGainMultiplier);

	UFUNCTION()
	void OnRep_IdentityGainMultiplier(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Attack, Category = "Damage")
		FGameplayAttributeData Attack;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, Attack);

	UFUNCTION()
	void OnRep_Attack(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Defense, Category = "Damage")
		FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, Defense);

	UFUNCTION()
	void OnRep_Defense(const FGameplayAttributeData& OldValue);

	/** Percentage modifiers used by the imported Gladiator class effects. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeedPercent, Category = "Gladiator|Combat")
	FGameplayAttributeData MoveSpeedPercent;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, MoveSpeedPercent);

	UFUNCTION()
	void OnRep_MoveSpeedPercent(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackSpeedPercent, Category = "Gladiator|Combat")
	FGameplayAttributeData AttackSpeedPercent;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, AttackSpeedPercent);

	UFUNCTION()
	void OnRep_AttackSpeedPercent(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DrainLifePercent, Category = "Gladiator|Combat")
	FGameplayAttributeData DrainLifePercent;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, DrainLifePercent);

	UFUNCTION()
	void OnRep_DrainLifePercent(const FGameplayAttributeData& OldValue);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DamageReductionPercent, Category = "Gladiator|Combat")
	FGameplayAttributeData DamageReductionPercent;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, DamageReductionPercent);

	UFUNCTION()
	void OnRep_DamageReductionPercent(const FGameplayAttributeData& OldValue);

	/** Temporary capture channel used by D1's active-effect duration execution. */
	UPROPERTY(BlueprintReadOnly, Category = "Gladiator|Combat", meta = (HideFromModifiers))
	FGameplayAttributeData ActiveEffectDuration;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, ActiveEffectDuration);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TakeDamage, Category = "Damage")
		FGameplayAttributeData TakeDamage;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, TakeDamage);

	UFUNCTION()
	void OnRep_TakeDamage(const FGameplayAttributeData& OldValue);

	// 치명타 확률 (0 ~ 1 범위)
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CriticalChance, Category = "Critical")
	FGameplayAttributeData CriticalChance;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, CriticalChance);

	UFUNCTION()
	void OnRep_CriticalChance(const FGameplayAttributeData& OldValue);

	// 치명타 데미지 배수 (예: 2.0은 2배 데미지)
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CriticalDamage, Category = "Critical")
	FGameplayAttributeData CriticalDamage;
	ATTRIBUTE_ACCESSORS(URPGAttributeSet, CriticalDamage); 

	UFUNCTION()
	void OnRep_CriticalDamage(const FGameplayAttributeData& OldValue);

private:
	TWeakInterfacePtr<IPawnUIInterface> CachedPawnUIInterface;
};
