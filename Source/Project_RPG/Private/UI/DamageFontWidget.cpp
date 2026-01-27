// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/DamageFontWidget.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"

void UDamageFontWidget::SetFontText(FText DamageText)
{
    if (DamageTextBlock)
    {
        DamageTextBlock->SetText(DamageText);
    }
}

void UDamageFontWidget::SetPositionInWorld(FVector Position)
{
    // 여기서 데미지 폰트를 타격 지점으로 설정 (월드 좌표로)
    PositionInWorld = Position;
    // 이후 위젯을 월드 좌표에 맞게 조정하는 코드 추가 가능
}

void UDamageFontWidget::SetTextColor(FLinearColor NewColor)
{
    if (DamageTextBlock) // DamageTextBlock은 FTextBlockWidget을 나타내는 멤버입니다
    {
        DamageTextBlock->SetColorAndOpacity(NewColor); // 텍스트 색상 변경
    }
}

void UDamageFontWidget::InitializeDamageFont(float InDamage, FVector Location, bool bCriticalAttack, bool bIsPlayerDamage)
{
    AddToViewport();

    SetFontText(FText::AsNumber(InDamage));
    SetPositionInWorld(Location);

    if (bCriticalAttack==true)
    {
        SetTextColor(FLinearColor::Yellow);
    }

    else if (bCriticalAttack==false)
    {
        SetTextColor(FLinearColor::White); // 일반 공격
    }

    if (bIsPlayerDamage)
    {
        SetTextColor(FLinearColor::Red);
    }

    if (FontAnim)
    {
        PlayAnimation(FontAnim);
    }

    GetWorld()->GetTimerManager().SetTimer(
        DestroyTimerHandle, this, &ThisClass::DestroyFont, 1.5f, false
    );
}

void UDamageFontWidget::InitializeInvincibleText(FVector Location)
{
    AddToViewport();

    SetFontText(FText::FromString("Invincible"));
    SetPositionInWorld(Location);

    SetTextColor(FLinearColor::White);

    PlayAnimation(FontAnim);

    GetWorld()->GetTimerManager().SetTimer(
        DestroyTimerHandle, this, &ThisClass::DestroyFont, 1.5f, false
    );
}

void UDamageFontWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    FVector2D ScreenPos;
    if (UGameplayStatics::ProjectWorldToScreen(GetWorld()->GetFirstPlayerController(), PositionInWorld, ScreenPos))
    {
        SetPositionInViewport(ScreenPos);
    }
}

void UDamageFontWidget::DestroyFont()
{
    RemoveFromParent(); // UI 제거
    ConditionalBeginDestroy(); // 메모리 해제
}
