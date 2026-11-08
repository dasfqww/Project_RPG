#include "UI/ActionBar/RPGActionBarIdentityWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> URPGActionBarIdentityWidget::RebuildWidget()
{
    if (WidgetTree && WidgetTree->RootWidget) return Super::RebuildWidget();
    TWeakObjectPtr<URPGActionBarIdentityWidget> Weak(this);
    return SNew(SVerticalBox)
        .Visibility_Lambda([Weak] {
            const auto* W = Weak.Get();
            return W && W->ReadIdentity().bVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
        })
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill)[
            SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)).Justification(ETextJustify::Center)
            .AutoWrapText(true).Text_Lambda([Weak] {
                const auto* W = Weak.Get(); return W ? W->ReadIdentity().Title : FText::GetEmpty();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[
            SNew(STextBlock).Justification(ETextJustify::Center).Text_Lambda([Weak] {
                const auto* W = Weak.Get(); return W ? W->ReadIdentity().Status : FText::GetEmpty();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill)[
            SNew(STextBlock).Justification(ETextJustify::Center).AutoWrapText(true).Text_Lambda([Weak] {
                const auto* W = Weak.Get(); return W ? W->ReadIdentity().Detail : FText::GetEmpty();
            })
        ];
}
