// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Type/RPGEnumTypes.h"
#include "RPGANS_OverlayEffect.generated.h"

USTRUCT()
struct FOverlayEffectProgressInfo
{
	GENERATED_BODY()

public:
	UPROPERTY()
	float ElapsedTime = 0.f;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> OverlayMaterialInstance;

	UPROPERTY()
	TArray<TWeakObjectPtr<UMeshComponent>> MeshComponents;
};

/**
 * 
 */
UCLASS(editinlinenew, Const, hideCategories = Object, collapseCategories, Meta = (DisplayName = "Weapon Overlay Effect"))
class PROJECT_RPG_API URPGANS_OverlayEffect : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	URPGANS_OverlayEffect(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void NotifyBegin(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, float TotalDuration, 
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, float FrameDeltaTime, 
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(class USkeletalMeshComponent* MeshComponent, 
		class UAnimSequenceBase* Animation, 
		const FAnimNotifyEventReference& EventReference) override;
	
protected:
	UPROPERTY(EditAnywhere)
	EOverlayTargetType OverlayTargetType = EOverlayTargetType::None;

	UPROPERTY(EditAnywhere, meta = (EditCondition = "OverlayTargetType == EOverlayTargetType::Weapon", EditConditionHides))
	EWeaponHandType WeaponHandType = EWeaponHandType::LeftHand;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UCurveLinearColor> LinearColorCurve;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UMaterialInterface> OverlayMaterial;

	UPROPERTY(EditAnywhere)
	FName ParameterName = "Color";

	UPROPERTY(EditAnywhere)
	bool bApplyRateScaleToProgress = true;

	UPROPERTY()
	TMap<TWeakObjectPtr<UMeshComponent>, FOverlayEffectProgressInfo> ProgressInfoMap;

};
