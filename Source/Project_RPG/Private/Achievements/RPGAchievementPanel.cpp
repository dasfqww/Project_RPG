#include "Achievements/RPGAchievementPanel.h"
#include "Achievements/RPGAchievementSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> URPGAchievementPanel::RebuildWidget()
{
    TWeakObjectPtr<URPGAchievementSubsystem> Owner = GetOwningLocalPlayer()
        ? GetOwningLocalPlayer()->GetSubsystem<URPGAchievementSubsystem>() : nullptr;
    return SNew(SExpandableArea)
        .InitiallyCollapsed(true)
        .HeaderPadding(FMargin(12, 9))
        .Padding(FMargin(14, 8))
        .OnAreaExpansionChanged_Lambda([Owner](bool bExpanded)
        {
            if (bExpanded && Owner.IsValid()) Owner->Refresh();
        })
        .HeaderContent()[SNew(STextBlock).Text(NSLOCTEXT("RPGAchievements", "Roster", "원정대 업적"))]
        .BodyContent()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([Owner]
        {
            return Owner.IsValid() ? Owner->GetSummary() : FText::GetEmpty();
        })];
}
