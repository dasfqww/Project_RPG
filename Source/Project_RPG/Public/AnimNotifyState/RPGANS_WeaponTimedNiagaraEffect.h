// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimNotifyState_TimedNiagaraEffect.h"
#include "Type/RPGEnumTypes.h"
#include "RPGANS_WeaponTimedNiagaraEffect.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGANS_WeaponTimedNiagaraEffect : public UAnimNotifyState_TimedNiagaraEffect
{
	GENERATED_BODY()
public:
	URPGANS_WeaponTimedNiagaraEffect
		(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void NotifyBegin(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, float TotalDuration, 
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, 
		const FAnimNotifyEventReference& EventReference) override;

private:
	USkeletalMeshComponent* GetWeaponMeshComponent(USkeletalMeshComponent* CharacterMeshComponent) const;

protected:
	UPROPERTY(EditAnywhere)
	EWeaponHandType WeaponHandType = EWeaponHandType::LeftHand;
};
