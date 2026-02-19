// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotifyState/RPGANS_OverlayEffect.h"


URPGANS_OverlayEffect::URPGANS_OverlayEffect(const FObjectInitializer& ObjectInitializer)
{
	//Super::URPGANS_OverlayEffect(ObjectInitializer);

}

void URPGANS_OverlayEffect::NotifyBegin(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComponent, Animation, TotalDuration, EventReference);


}

void URPGANS_OverlayEffect::NotifyTick(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComponent, Animation, FrameDeltaTime, EventReference);


}

void URPGANS_OverlayEffect::NotifyEnd(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComponent, Animation, EventReference);


}
