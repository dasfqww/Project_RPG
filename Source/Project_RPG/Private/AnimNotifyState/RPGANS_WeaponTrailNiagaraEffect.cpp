// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimNotifyState/RPGANS_WeaponTrailNiagaraEffect.h"
#include "NiagaraComponent.h"

#include "Character/RPGBaseCharacter.h"


URPGANS_WeaponTrailNiagaraEffect::URPGANS_WeaponTrailNiagaraEffect
	(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif

	Template = nullptr;
	LocationOffset.Set(0.0f, 0.0f, 0.0f);
	RotationOffset = FRotator(0.0f, 0.0f, 0.0f);
}

void URPGANS_WeaponTrailNiagaraEffect::NotifyBegin(USkeletalMeshComponent* MeshComponent,
	UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	USkeletalMeshComponent* WeaponMeshComponent = GetWeaponMeshComponent(MeshComponent);
	Super::NotifyBegin(WeaponMeshComponent ? 
		WeaponMeshComponent : MeshComponent, Animation, TotalDuration, EventReference);

	UpdateNiagaraParameters(WeaponMeshComponent);
}

void URPGANS_WeaponTrailNiagaraEffect::NotifyTick(USkeletalMeshComponent* MeshComponent, 
	UAnimSequenceBase* Animation, float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	USkeletalMeshComponent* WeaponMeshComponent = GetWeaponMeshComponent(MeshComponent);
	Super::NotifyTick(WeaponMeshComponent ? 
		WeaponMeshComponent : MeshComponent, Animation, FrameDeltaTime, EventReference);

	UpdateNiagaraParameters(WeaponMeshComponent);
}

void URPGANS_WeaponTrailNiagaraEffect::NotifyEnd(USkeletalMeshComponent* MeshComponent,
	UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	USkeletalMeshComponent* WeaponMeshComponent = GetWeaponMeshComponent(MeshComponent);
	Super::NotifyEnd(WeaponMeshComponent ? 
		WeaponMeshComponent : MeshComponent, Animation, EventReference);

	UpdateNiagaraParameters(WeaponMeshComponent);
}

void URPGANS_WeaponTrailNiagaraEffect::UpdateNiagaraParameters(USkeletalMeshComponent* WeaponMeshComponent)
{
	if (!IsValid(WeaponMeshComponent)) return;

	UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(GetSpawnedEffect(WeaponMeshComponent));
	if (!IsValid(NiagaraComponent)) return;

	if (WeaponMeshComponent->DoesSocketExist(StartSocketName))
	{
		NiagaraComponent->SetVectorParameter(StartParameterName, WeaponMeshComponent->GetSocketLocation(StartSocketName));
	}

	if (WeaponMeshComponent->DoesSocketExist(EndSocketName))
	{
		NiagaraComponent->SetVectorParameter(EndParameterName, WeaponMeshComponent->GetSocketLocation(EndSocketName));
	}
}

USkeletalMeshComponent* URPGANS_WeaponTrailNiagaraEffect::GetWeaponMeshComponent(USkeletalMeshComponent* CharacterMeshComponent) const
{
	return nullptr;
}
