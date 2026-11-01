#include "UI/Loading/RPGExperienceLoadingScreen.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Manager/Component/RPGExperienceManagerComponent.h"
#include "Settings/RPGExperienceSettings.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RPGExperienceUI"

URPGExperienceLoadingScreen::URPGExperienceLoadingScreen(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

bool URPGExperienceLoadingScreen::IsExperienceFailed() const
{
    const UWorld* World = GetWorld();
    const AGameStateBase* State = World ? World->GetGameState() : nullptr;
    const URPGExperienceManagerComponent* Experience = IsValid(State)
        ? State->FindComponentByClass<URPGExperienceManagerComponent>() : nullptr;
    return IsValid(Experience) && Experience->GetLoadState() == ERPGExperienceLoadState::Failed;
}

FText URPGExperienceLoadingScreen::GetTitleText() const
{
    return IsExperienceFailed() ? LOCTEXT("FailedTitle", "게임을 시작할 수 없습니다")
        : LOCTEXT("PreparingTitle", "게임을 준비하고 있습니다");
}

FText URPGExperienceLoadingScreen::GetMessageText() const
{
    return IsExperienceFailed() ? GetDefault<URPGExperienceSettings>()->LoadingFailureMessage
        : LOCTEXT("PreparingMessage", "잠시만 기다려 주세요.");
}

TSharedRef<SWidget> URPGExperienceLoadingScreen::RebuildWidget()
{
    const TWeakObjectPtr<URPGExperienceLoadingScreen> WeakThis(this);
    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
        .BorderBackgroundColor(FLinearColor(0.018f, 0.024f, 0.035f, 1.f))
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .Padding(FMargin(48.f))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 22.f)
            [
                SNew(SThrobber)
                .NumPieces(3)
                .Visibility_Lambda([WeakThis]()
                {
                    const URPGExperienceLoadingScreen* Screen = WeakThis.Get();
                    return Screen && !Screen->IsExperienceFailed()
                        ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                })
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [
                SNew(STextBlock)
                .Text_Lambda([WeakThis]()
                {
                    const URPGExperienceLoadingScreen* Screen = WeakThis.Get();
                    return Screen ? Screen->GetTitleText() : FText::GetEmpty();
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
                .ColorAndOpacity(FLinearColor(0.94f, 0.96f, 1.f))
                .Justification(ETextJustify::Center)
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
            [
                SNew(STextBlock)
                .Text_Lambda([WeakThis]()
                {
                    const URPGExperienceLoadingScreen* Screen = WeakThis.Get();
                    return Screen ? Screen->GetMessageText() : FText::GetEmpty();
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
                .ColorAndOpacity(FLinearColor(0.72f, 0.78f, 0.86f))
                .Justification(ETextJustify::Center)
                .WrapTextAt(640.f)
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
