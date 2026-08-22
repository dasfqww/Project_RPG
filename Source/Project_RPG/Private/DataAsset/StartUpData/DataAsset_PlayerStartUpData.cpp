// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/StartUpData/DataAsset_PlayerStartUpData.h"
#include "Ability/RPGGameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "RPGGameplayTags.h"

void UDataAsset_PlayerStartUpData::GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);

	for (const FRPGPlayerAbilitySet& AbilitySet : PlayerStartUpAbilitySets)
	{
		if (!AbilitySet.IsValid())continue;

		FGameplayAbilitySpec AbilitySpec(AbilitySet.AbilityToGrant);
		AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
		AbilitySpec.Level = ApplyLevel;
		AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilitySet.InputTag);

		InASCToGive->GiveAbility(AbilitySpec);
	}

	// 아이덴티티 어빌리티 부여 (Z키 매핑)
	if (IdentityData.IdentityAbility)
	{
		FGameplayAbilitySpec IdentitySpec(IdentityData.IdentityAbility);
		IdentitySpec.SourceObject = InASCToGive->GetAvatarActor();
		IdentitySpec.Level = ApplyLevel;
		IdentitySpec.GetDynamicSpecSourceTags().AddTag(RPGGameplayTags::InputTag_IdentitySkill);

		InASCToGive->GiveAbility(IdentitySpec);
	}
}
