// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "DamageFontWidget.generated.h"

class UTextBlock;
class UWidgetAnimation;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDamageFontWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable)
    void SetFontText(FText DamageText);

    UFUNCTION(BlueprintCallable)
    void SetPositionInWorld(FVector Position);

    UFUNCTION(BlueprintCallable, Category = "Damage Font")
    void SetTextColor(FLinearColor NewColor);

    void InitializeDamageFont(float Damage, FVector Location, bool bCriticalAttack, bool bIsPlayerDamage);

    void InitializeInvincibleText(FVector Location);

protected:
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    void DestroyFont();

private:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> DamageTextBlock; // 데미지 텍스트

    // 애니메이션을 바인딩하기 위해 이 속성을 사용합니다.
    UPROPERTY(meta = (BindWidgetAnim), Transient)
    TObjectPtr<UWidgetAnimation> FontAnim;

    FVector PositionInWorld;  // 데미지 텍스트의 화면 상 위치

    FTimerHandle DestroyTimerHandle;

public:
    FORCEINLINE UWidgetAnimation* GetDamageFontAnim() const { return FontAnim; }
};
