// Fill out your copyright notice in the Description page of Project Settings.

#include "DataAsset/Team/RPGTeamDisplayAsset.h"
#include "Components/MeshComponent.h"
#include "NiagaraComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGTeamDisplayAsset)

void URPGTeamDisplayAsset::ApplyToMeshComponent(UMeshComponent* MeshComponent)
{
	if (!MeshComponent) return;

	// 레이드에서는 전체 머티리얼을 덮어쓰기보다 
	// 커스텀 스텐실 값이나 특정 파라미터만 조정하는 방식이 일반적입니다.
	
	// 예: 외곽선 색상 파라미터가 있는 경우 적용
	MeshComponent->SetVectorParameterValueOnMaterials(TEXT("OutlineColor"), FVector(OutlineColor));
	
	// 필요시 커스텀 스텐실 값 등을 팀별로 다르게 설정할 수 있습니다.
	// MeshComponent->SetCustomDepthStencilValue(1); 
}

void URPGTeamDisplayAsset::ApplyToNiagaraComponent(UNiagaraComponent* NiagaraComponent)
{
	if (!NiagaraComponent || !TeamEffect) return;

	NiagaraComponent->SetAsset(TeamEffect);
	NiagaraComponent->SetVariableLinearColor(TEXT("TeamColor"), TeamColor);
}
