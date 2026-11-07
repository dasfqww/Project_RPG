#include "UI/ActionBar/SRPGActionBarCell.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RPGActionBar"

void SRPGActionBarCell::Construct(const FArguments& Args)
{
    Label = Args._Label;
    View = Args._View;
    IsHeld = Args._IsHeld;
    const TWeakPtr<SRPGActionBarCell> WeakCell = SharedThis(this);
    SetToolTipText(TAttribute<FText>::CreateLambda([WeakCell] {
        const auto Cell = WeakCell.Pin();
        if (!Cell) return FText::GetEmpty();
        const auto Current = Cell->View.Get(FRPGActionBarActionView{});
        if (Current.bAvailable) return Cell->Label;
        const FText Reason = Current.UnavailableReason.IsEmpty()
            ? LOCTEXT("Unavailable", "현재 사용할 수 없습니다.") : Current.UnavailableReason;
        return Cell->Label.IsEmpty() ? Reason : FText::Format(LOCTEXT("UnavailableToolTip", "{0}\n{1}"), Cell->Label, Reason);
    }));
    ChildSlot[
        SNew(SBox).WidthOverride(72.f).HeightOverride(82.f)[
            SNew(SButton).IsFocusable(false)
            .IsEnabled_Lambda([WeakCell] {
                const auto Cell = WeakCell.Pin();
                return Cell && (Cell->IsHeld.Get(false) || Cell->View.Get(FRPGActionBarActionView{}).bAvailable);
            })
            .OnPressed(Args._OnPressed)
            .OnReleased(Args._OnReleased)[
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SImage).Image(Args._Icon)]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(Label)]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[
                    SNew(STextBlock).Text_Lambda([WeakCell] {
                        const auto Cell = WeakCell.Pin();
                        if (!Cell) return FText::GetEmpty();
                        const auto Current = Cell->View.Get(FRPGActionBarActionView{});
                        // Provider views normally arrive sanitized by the context; avoid overflow for large values too.
                        if (FMath::IsFinite(Current.CooldownRemaining) && Current.CooldownRemaining > 0.f)
                            return FText::AsNumber(FMath::CeilToDouble(static_cast<double>(Current.CooldownRemaining)));
                        return Current.KeyLabel;
                    })
                ]
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
