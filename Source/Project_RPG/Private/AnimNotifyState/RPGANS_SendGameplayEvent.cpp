// Fill out your copyright notice in the Description page of Project Settings.

#include "AnimNotifyState/RPGANS_SendGameplayEvent.h"

#include "AbilitySystemBlueprintLibrary.h"

URPGANS_SendGameplayEvent::URPGANS_SendGameplayEvent(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
	bIsNativeBranchingPoint = true;
}

void URPGANS_SendGameplayEvent::NotifyBegin(USkeletalMeshComponent* MeshComponent, 
	UAnimSequenceBase* Animation, float TotalDuration, 
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComponent, Animation, TotalDuration, EventReference);

	if (MeshComponent == nullptr || MeshComponent->GetOwner() == nullptr) return;

	if (BeginEventTag.IsValid())
	{
		UAbilitySystemBlueprintLibrary::
			SendGameplayEventToActor(MeshComponent->GetOwner(), BeginEventTag, EventData);
	}
}

void URPGANS_SendGameplayEvent::NotifyTick(USkeletalMeshComponent* MeshComponent, 
	UAnimSequenceBase* Animation, float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComponent, Animation, FrameDeltaTime, EventReference);

	if (MeshComponent == nullptr || MeshComponent->GetOwner() == nullptr) return;

	if (TickEventTag.IsValid())
	{
		UAbilitySystemBlueprintLibrary::
			SendGameplayEventToActor(MeshComponent->GetOwner(), TickEventTag, EventData);
	}
}

void URPGANS_SendGameplayEvent::NotifyEnd(USkeletalMeshComponent* MeshComponent,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComponent, Animation, EventReference);

	if (MeshComponent == nullptr || MeshComponent->GetOwner() == nullptr) return;

	if (EndEventTag.IsValid())
	{
		UAbilitySystemBlueprintLibrary::
			SendGameplayEventToActor(MeshComponent->GetOwner(), EndEventTag, EventData);
	}
}
