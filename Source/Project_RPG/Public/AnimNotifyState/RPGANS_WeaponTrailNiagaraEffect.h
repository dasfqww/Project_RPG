// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimNotifyState_TimedNiagaraEffect.h"
#include "Type/RPGEnumTypes.h"
#include "RPGANS_WeaponTrailNiagaraEffect.generated.h"

/**
 * 
 */
UCLASS(Blueprintable, meta = (DisplayName = "Weapon Trail Niagara Effect"))
class PROJECT_RPG_API URPGANS_WeaponTrailNiagaraEffect : public UAnimNotifyState_TimedNiagaraEffect
{
	GENERATED_BODY()
public:
	URPGANS_WeaponTrailNiagaraEffect
		(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void NotifyBegin(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, float TotalDuration, 
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComponent, 
		UAnimSequenceBase* Animation, float FrameDeltaTime, 
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	UPROPERTY(EditAnywhere)
	EWeaponHandType WeaponHandType = EWeaponHandType::LeftHand;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NiagaraSystem, meta = (ToolTip = "The socket or bone to attach the system to", AnimNotifyBoneName = "true"))
	FName StartSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NiagaraSystem)
	FName StartParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NiagaraSystem, meta = (ToolTip = "The socket or bone to attach the system to", AnimNotifyBoneName = "true"))
	FName EndSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = NiagaraSystem)
	FName EndParameterName;

private:
	void UpdateNiagaraParameters(USkeletalMeshComponent* WeaponMeshComponent);
	USkeletalMeshComponent* GetWeaponMeshComponent(USkeletalMeshComponent* CharacterMeshComponent) const;
};
