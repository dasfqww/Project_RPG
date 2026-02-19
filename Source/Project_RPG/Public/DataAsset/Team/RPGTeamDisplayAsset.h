// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RPGTeamDisplayAsset.generated.h"

class UTexture2D;
class UMeshComponent;
class UNiagaraSystem;

/**
 * URPGTeamDisplayAsset: 팀/파티의 시각적 테마 및 레이드 전술 정보를 정의하는 에셋
 */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGTeamDisplayAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- UI & Identification ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FText TeamName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FLinearColor TeamColor = FLinearColor::White; // HP바, 네임플레이트 등에 사용

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UTexture2D> TeamIcon; // 미니맵이나 파티 리스트용 아이콘

	// --- Raid Tactics ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Raid")
	TObjectPtr<UTexture2D> DefaultRaidMarker; // 기본 타겟팅 징표

	// --- In-Game Visuals ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
	FLinearColor OutlineColor = FLinearColor::Black; // 캐릭터 외곽선(Stencil) 색상

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals")
	TObjectPtr<UNiagaraSystem> TeamEffect; // 팀 전용 이펙트 (발밑 오라 등)

public:
	/** 메쉬 컴포넌트의 특정 파라미터(예: 외곽선 색상)만 업데이트합니다. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Teams")
	void ApplyToMeshComponent(UMeshComponent* MeshComponent);

	/** 나이아가라 컴포넌트에 팀 이펙트를 설정하고 색상 파라미터를 전달합니다. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Teams")
	void ApplyToNiagaraComponent(UNiagaraComponent* NiagaraComponent);
};
