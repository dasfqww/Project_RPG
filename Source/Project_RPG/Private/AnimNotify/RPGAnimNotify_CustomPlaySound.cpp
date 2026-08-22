// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotify/RPGAnimNotify_CustomPlaySound.h"
#include "Manager/SoundManager.h"

void URPGAnimNotify_CustomPlaySound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (Sound&&MeshComp)
	{
		FVector Location = MeshComp->GetComponentLocation();

		USoundManager::Get()->Play(Sound, Location);
	}
}
