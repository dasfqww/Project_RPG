#include "UI/ActionBar/SRPGActionBarCell.h"
#include "UI/ActionBar/RPGActionBarLayout.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Slate/DeferredCleanupSlateBrush.h"

#define LOCTEXT_NAMESPACE "RPGActionBar"

namespace
{
bool HasKnownCooldown(const FRPGActionBarActionView& View)
{
    return FMath::IsFinite(View.CooldownRemaining) && View.CooldownRemaining > 0.f
        && FMath::IsFinite(View.CooldownDuration) && View.CooldownDuration > 0.f;
}
}

void SRPGActionBarCell::Construct(const FArguments& Args)
{
    Label = Args._Label;
    View = Args._View;
    IsHeld = Args._IsHeld;
    // The image may outlive both its cell and the UMG bar during a profile change.
    // Copy authoring data and retain its UObject resource through deferred render cleanup.
    TSharedPtr<FDeferredCleanupSlateBrush> Icon;
    if (Args._Icon) Icon = FDeferredCleanupSlateBrush::CreateBrush(*Args._Icon);
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
        SNew(SBox).WidthOverride(RPGActionBarLayout::CellWidth).HeightOverride(RPGActionBarLayout::CellHeight)[
            SNew(SButton).IsFocusable(false)
            .IsEnabled_Lambda([WeakCell] {
                const auto Cell = WeakCell.Pin();
                return Cell && (Cell->IsHeld.Get(false) || Cell->View.Get(FRPGActionBarActionView{}).bAvailable);
            })
            .OnPressed(Args._OnPressed)
            .OnReleased(Args._OnReleased)[
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[
                    SNew(SImage).Image_Lambda([Icon] { return FDeferredCleanupSlateBrush::TrySlateBrush(Icon); })
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(Label)]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[
                    SNew(STextBlock).Text_Lambda([WeakCell] {
                        const auto Cell = WeakCell.Pin();
                        if (!Cell) return FText::GetEmpty();
                        const auto Current = Cell->View.Get(FRPGActionBarActionView{});
                        // Provider views normally arrive sanitized by the context; avoid overflow for large values too.
                        if (FMath::IsFinite(Current.CooldownRemaining) && Current.CooldownRemaining > 0.f)
                        {
                            const FText Remaining = FText::AsNumber(FMath::CeilToDouble(static_cast<double>(Current.CooldownRemaining)));
                            return Current.KeyLabel.IsEmpty() ? Remaining
                                : FText::Format(LOCTEXT("KeyAndCooldown", "{0} · {1}"), Current.KeyLabel, Remaining);
                        }
                        return Current.KeyLabel;
                    })
                ]
                + SVerticalBox::Slot().AutoHeight()[
                    SNew(SBox).HeightOverride(4.f)[
                        SNew(SProgressBar).Style(&FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar"))
                        .Visibility_Lambda([WeakCell] {
                            const auto Cell = WeakCell.Pin();
                            return Cell && HasKnownCooldown(Cell->View.Get(FRPGActionBarActionView{}))
                                ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                        })
                        .Percent_Lambda([WeakCell]() -> TOptional<float> {
                            const auto Cell = WeakCell.Pin();
                            const auto Current = Cell ? Cell->View.Get(FRPGActionBarActionView{}) : FRPGActionBarActionView{};
                            if (!HasKnownCooldown(Current)) return 0.f; // Never select indeterminate/marquee mode.
                            return static_cast<float>(FMath::Clamp(static_cast<double>(Current.CooldownRemaining)
                                / static_cast<double>(Current.CooldownDuration), 0., 1.));
                        })
                    ]
                ]
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
