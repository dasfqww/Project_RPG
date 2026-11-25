// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Combat/NPCCombatComponent.h"
#include "Ability/Skill/RPGGreatSwordCounterAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "RPGGameplayTags.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
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
		URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(HitActor, RPGGameplayTags::Player_Status_Blocking);
	const bool bIsMyAttackUnblockable =
		URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(GetOwningPawn(), RPGGameplayTags::NPC_Status_Unblockable);

	if (!bIsMyAttackUnblockable)
	{
		auto* Attacker = GetOwningPawn();
		auto* Defender = Cast<APawn>(HitActor);
		auto* ASC = Defender ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Defender) : nullptr;
		auto* Counter = ASC ? Cast<URPGGreatSwordCounterAbility>(ASC->GetAnimatingAbility()) : nullptr;
		// This hook receives native NPC weapon and hand overlaps. Area effects
		// and projectile impacts dispatch through their own server paths.
		if (Counter && Attacker && Attacker->HasAuthority() && Counter->TryInterceptWeaponHit(*Attacker, *Defender))
		{
			return;
		}
	}

	if (bIsPlayerBlocking&&!bIsMyAttackUnblockable)
	{
		bIsValidBlock = URPGCombatFunctionLibrary::IsValidBlock(GetOwningPawn(), HitActor);
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
