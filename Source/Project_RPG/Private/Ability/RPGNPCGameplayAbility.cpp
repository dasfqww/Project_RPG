// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/RPGNPCGameplayAbility.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "RPGGameplayTags.h"

ARPGNonPlayerCharacter* URPGNPCGameplayAbility::GetNonPlayerCharacterFromActorInfo()
{
	if (!CachedRPGNonPlayerCharacter.IsValid())
	{
		CachedRPGNonPlayerCharacter = Cast<ARPGNonPlayerCharacter>(CurrentActorInfo->AvatarActor);
	}

	return CachedRPGNonPlayerCharacter.IsValid() ? CachedRPGNonPlayerCharacter.Get() : nullptr;
}

UNPCCombatComponent* URPGNPCGameplayAbility::GetNPCombatComponentFromActorInfo()
{
	return GetNonPlayerCharacterFromActorInfo()->GetNPCCombatComponent();
}

FGameplayEffectSpecHandle URPGNPCGameplayAbility::MakeEnemyDamageEffectSpecHandle(TSubclassOf<UGameplayEffect> EffectClass,
	const FScalableFloat& InDamageScalableFloat)
{
	check(EffectClass);

	FGameplayEffectContextHandle ContextHandle = GetRPGAbilitySystemComponentFromActorInfo()->MakeEffectContext();
	ContextHandle.SetAbility(this);
	ContextHandle.AddSourceObject(GetAvatarActorFromActorInfo());
	ContextHandle.AddInstigator(GetAvatarActorFromActorInfo(), GetAvatarActorFromActorInfo());

	FGameplayEffectSpecHandle EffectSpecHandle = GetRPGAbilitySystemComponentFromActorInfo()->MakeOutgoingSpec(
		EffectClass,
		GetAbilityLevel(),
		ContextHandle
	);

	EffectSpecHandle.Data->SetSetByCallerMagnitude(
		RPGGameplayTags::Shared_SetByCaller_BaseDamage,
		InDamageScalableFloat.GetValueAtLevel(GetAbilityLevel())
	);

	return EffectSpecHandle;
}
