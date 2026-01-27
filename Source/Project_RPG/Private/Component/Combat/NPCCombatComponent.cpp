// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Combat/NPCCombatComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "RPGGameplayTags.h"
#include "RPGFunctionLibrary.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Components/BoxComponent.h"

#include "RPGDebugHelper.h"

void UNPCCombatComponent::OnHitTargetActor(AActor* HitActor)
{
	if (OverlappedActors.Contains(HitActor))
	{
		return;
	}

	OverlappedActors.AddUnique(HitActor);

	bool bIsValidBlock = false;

	const bool bIsPlayerBlocking =
		URPGFunctionLibrary::NativeDoesActorHaveTag(HitActor, RPGGameplayTags::Player_Status_Blocking);
	const bool bIsMyAttackUnblockable =
		URPGFunctionLibrary::NativeDoesActorHaveTag(GetOwningPawn(), RPGGameplayTags::NPC_Status_Unblockable);

	if (bIsPlayerBlocking&&!bIsMyAttackUnblockable)
	{
		bIsValidBlock = URPGFunctionLibrary::IsValidBlock(GetOwningPawn(), HitActor);
	}

	FGameplayEventData EventData;
	EventData.Instigator = GetOwningPawn();
	EventData.Target = HitActor;

	if (bIsValidBlock)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			HitActor,
			RPGGameplayTags::Player_Event_SuccessfulBlock,
			EventData
		);
	}

	else
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			GetOwningPawn(),
			RPGGameplayTags::Shared_Event_Hit,
			EventData
		);
	}
}

void UNPCCombatComponent::ToggleBodyCollsionBoxCollision(bool bShouldEnable, EToggleDamageType ToggleDamageType)
{
	ARPGNonPlayerCharacter* OwningNPC = GetOwningPawn<ARPGNonPlayerCharacter>();

	check(OwningNPC);

	UBoxComponent* LeftHandCollisionBox = OwningNPC->GetLeftHandCollisionBox();
	UBoxComponent* RightHandCollisionBox = OwningNPC->GetRightHandCollisionBox();

	check(LeftHandCollisionBox && RightHandCollisionBox);

	switch (ToggleDamageType)
	{
	case EToggleDamageType::LeftHand:
		LeftHandCollisionBox->SetCollisionEnabled(bShouldEnable ? ECollisionEnabled::QueryOnly 
			: ECollisionEnabled::NoCollision);
		break;

	case EToggleDamageType::RightHand:
		RightHandCollisionBox->SetCollisionEnabled(bShouldEnable ? ECollisionEnabled::QueryOnly 
			: ECollisionEnabled::NoCollision);
		break;

	default:
		break;
	}

	if (!bShouldEnable)
	{
		OverlappedActors.Empty();
	}
}
