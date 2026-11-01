#include "UI/Loading/RPGExperienceFailureNotice.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RPGExperienceUI"

URPGExperienceFailureNotice::URPGExperienceFailureNotice(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> URPGExperienceFailureNotice::RebuildWidget()
{
    const TWeakObjectPtr<URPGExperienceFailureNotice> WeakThis(this);
    return SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(28.f))
        [
            SNew(SBox).MaxDesiredWidth(720.f)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                .BorderBackgroundColor(FLinearColor(0.11f, 0.04f, 0.045f, 0.94f))
                .Padding(FMargin(24.f, 16.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("FailedTitle", "게임을 시작할 수 없습니다"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
                        .ColorAndOpacity(FLinearColor(1.f, 0.84f, 0.84f))
                        .Justification(ETextJustify::Center)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([WeakThis]()
                        {
                            const URPGExperienceFailureNotice* Notice = WeakThis.Get();
                            return Notice ? Notice->NoticeMessage : FText::GetEmpty();
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
                        .ColorAndOpacity(FLinearColor(0.94f, 0.94f, 0.96f))
                        .Justification(ETextJustify::Center)
                        .WrapTextAt(640.f)
                    ]
                ]
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
