// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "RPGAnimNotify_SendGameplayEvent.generated.h"

/**
 * 
 */
UCLASS(meta = (DisplayName = "Send Gameplay Event"))
class PROJECT_RPG_API URPGAnimNotify_SendGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	URPGAnimNotify_SendGameplayEvent(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	/** Property names intentionally match D1 for serialized montage compatibility. */
	UPROPERTY(EditAnywhere, Category = "Gameplay Event")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, Category = "Gameplay Event")
	FGameplayEventData EventData;
};
