#include "UI/ActionBar/RPGActionBarLayout.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

bool RPGActionBarLayout::Resolve(TConstArrayView<int32> DisplaySlots, int32 FixedSlotCount, TArray<int32>& OutOrder)
{
    OutOrder.Reset();
    if (FixedSlotCount < 0 || FixedSlotCount > 8 || DisplaySlots.Num() > 8) return false;
    int32 RequiredCount = DisplaySlots.Num();
    TSet<int32> ExplicitSlots;
    for (int32 Slot : DisplaySlots)
    {
        if (Slot < INDEX_NONE || Slot >= 8 || (Slot != INDEX_NONE && ExplicitSlots.Contains(Slot))) return false;
        if (Slot != INDEX_NONE)
        {
            ExplicitSlots.Add(Slot);
            RequiredCount = FMath::Max(RequiredCount, Slot + 1);
        }
    }
    if (FixedSlotCount && RequiredCount > FixedSlotCount) return false;
    OutOrder.Init(INDEX_NONE, FixedSlotCount ? FixedSlotCount : RequiredCount);
    for (int32 I = 0; I < DisplaySlots.Num(); ++I)
        if (DisplaySlots[I] != INDEX_NONE) OutOrder[DisplaySlots[I]] = I;
    for (int32 I = 0; I < DisplaySlots.Num(); ++I)
        if (DisplaySlots[I] == INDEX_NONE) OutOrder[OutOrder.Find(INDEX_NONE)] = I;
    return true;
}

TSharedRef<SHorizontalBox> RPGActionBarLayout::CreateRow(TConstArrayView<int32> Order,
    TFunctionRef<TSharedRef<SWidget>(int32)> CreateCell)
{
    auto Row = SNew(SHorizontalBox);
    for (int32 ActionIndex : Order)
    {
        if (ActionIndex != INDEX_NONE)
            Row->AddSlot().AutoWidth().Padding(3.f)[CreateCell(ActionIndex)];
        else
            Row->AddSlot().AutoWidth().Padding(3.f)[
                SNew(SBox).WidthOverride(CellWidth).HeightOverride(CellHeight).Visibility(EVisibility::HitTestInvisible)[
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.2f))
                ]
            ];
    }
    return Row;
}
