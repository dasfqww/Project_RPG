// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Ability/RPGNPCGameplayAbility.h"

void UDataAsset_NPCStartUpData::GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);

	if (!NPCStartUpAbilities.IsEmpty())
	{
		for (const TSubclassOf<URPGNPCGameplayAbility>& AbilityClass : NPCStartUpAbilities)
		{
			FGameplayAbilitySpec AbilitySpec(AbilityClass);
			AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
			AbilitySpec.Level = ApplyLevel;

			InASCToGive->GiveAbility(AbilitySpec);
		}
	}
}
