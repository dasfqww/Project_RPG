// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "RPGANS_SendGameplayEvent.generated.h"

/**
 * 
 */
UCLASS(meta = (DisplayName = "Send Gameplay Event State"))
class PROJECT_RPG_API URPGANS_SendGameplayEvent : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	URPGANS_SendGameplayEvent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void NotifyBegin(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, float TotalDuration, 
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComponent, 
		UAnimSequenceBase* Animation, float FrameDeltaTime, 
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, 
		const FAnimNotifyEventReference& EventReference) override;
protected:
	UPROPERTY(EditAnywhere)
	FGameplayTag BeginEventTag;

	UPROPERTY(EditAnywhere)
	FGameplayTag TickEventTag;

	UPROPERTY(EditAnywhere)
	FGameplayTag EndEventTag;

	UPROPERTY(EditAnywhere)
	FGameplayEventData EventData;
};
