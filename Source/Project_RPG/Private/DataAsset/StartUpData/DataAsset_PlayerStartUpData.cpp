// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/StartUpData/DataAsset_PlayerStartUpData.h"
#include "Ability/RPGGameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "RPGGameplayTags.h"
#include "PlayerClass/RPGPlayerClassComponent.h"

void UDataAsset_PlayerStartUpData::GiveToAbilitySystemComponent(URPGAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	if (!InASCToGive || !InASCToGive->IsOwnerActorAuthoritative()) return;
	auto* ClassOwner = InASCToGive->GetAvatarActor()
		? InASCToGive->GetAvatarActor()->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
	if (ClassOwner) ClassOwner->ReserveStartupIdentity(IdentityData.IdentityAbility);
	Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);

	for (const FRPGPlayerAbilitySet& AbilitySet : PlayerStartUpAbilitySets)
	{
		if (!AbilitySet.IsValid())continue;
		if (ClassOwner && ClassOwner->RoutesAbilityToClass(AbilitySet.AbilityToGrant.Get(), AbilitySet.InputTag)) continue;

		FGameplayAbilitySpec AbilitySpec;
		if (!AbilitySet.BuildAbilitySpec(ApplyLevel, InASCToGive->GetAvatarActor(), AbilitySpec)) continue;

		InASCToGive->GiveAbility(AbilitySpec);
	}

	// 아이덴티티 어빌리티 부여 (Z키 매핑)
	if (!ClassOwner && IdentityData.IdentityAbility)
	{
		FGameplayAbilitySpec IdentitySpec(IdentityData.IdentityAbility);
		IdentitySpec.SourceObject = InASCToGive->GetAvatarActor();
		IdentitySpec.Level = ApplyLevel;
		IdentitySpec.GetDynamicSpecSourceTags().AddTag(RPGGameplayTags::InputTag_IdentitySkill);

		InASCToGive->GiveAbility(IdentitySpec);
	}
}
