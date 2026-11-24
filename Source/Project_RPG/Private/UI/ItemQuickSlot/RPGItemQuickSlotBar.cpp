#include "UI/ItemQuickSlot/RPGItemQuickSlotBar.h"
#include "UI/RPGQuickSlotWidget.h"
#include "Controller/RPGPlayerController.h"
#include "Component/UI/QuickSlotComponent.h"
#include "GameFramework/Pawn.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "UI/Riding/RPGMountQuickSlots.h"
#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> URPGItemQuickSlotBar::RebuildWidget()
{
    SetIsFocusable(false);
    Cells.Reset();
    const auto* Pawn = GetOwningPlayerPawn();
    const TWeakObjectPtr<UQuickSlotComponent> Slots(Pawn ? Pawn->FindComponentByClass<UQuickSlotComponent>() : nullptr);
    if (!Slots.IsValid()) return SNew(SHorizontalBox);
    for (int32 Index = 0; Index < UQuickSlotComponent::ItemSlotCount; ++Index)
    {
        auto* Cell = CreateWidget<URPGQuickSlotWidget>(GetOwningPlayer());
        if (!Cell) { Cells.Reset(); return SNew(SHorizontalBox); }
        Cell->ConfigureItemSlot(Index);
        Cells.Add(Cell);
    }
    auto Grid = SNew(SVerticalBox);
    for (int32 Row = 0; Row < 2; ++Row)
    {
        auto Line = SNew(SHorizontalBox);
        for (int32 Column = 0; Column < 4; ++Column)
            Line->AddSlot().AutoWidth().Padding(Column ? 4.f : 0.f, 0.f, 0.f, 0.f)
                [Cells[Row * 4 + Column]->TakeWidget()];
        Grid->AddSlot().AutoHeight().Padding(0.f, Row ? 4.f : 0.f, 0.f, 0.f)[Line];
    }
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
        [SNew(SBox).WidthOverride(64.f)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Cells[UQuickSlotComponent::PotionSlotIndex]->TakeWidget()]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
                [SNew(SButton).IsFocusable(false).ContentPadding(FMargin(2.f, 1.f)).HAlign(HAlign_Center)
                    .ToolTipText(NSLOCTEXT("ItemQuickSlots", "AutoHelp", "설정한 HP 이하에서 물약을 5초 간격으로 자동 사용합니다. 가방 조작·탑승·사망·복구 중에는 사용하지 않습니다."))
                    .IsEnabled_Lambda([Slots] { FRPGInventoryProjectionEntry Entry; return Slots.IsValid() && Slots->FindAssignedItem(UQuickSlotComponent::PotionSlotIndex, Entry); })
                    .OnClicked_Lambda([Slots] { if (Slots.IsValid()) Slots->SetPotionAutoUse(!Slots->IsPotionAutoUseEnabled()); return FReply::Handled(); })
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)).MinDesiredWidth(60.f).Justification(ETextJustify::Center)
                        .ColorAndOpacity_Lambda([Slots] { return Slots.IsValid() && Slots->IsPotionAutoUseEnabled()
                            ? FSlateColor(FLinearColor(.4f, 1.f, .5f)) : FSlateColor(FLinearColor(.7f, .65f, .55f)); })
                        .Text_Lambda([Slots] { return Slots.IsValid() && Slots->IsPotionAutoUseEnabled()
                            ? NSLOCTEXT("ItemQuickSlots", "AutoOn", "자동 ON") : NSLOCTEXT("ItemQuickSlots", "AutoOff", "자동 OFF"); })]]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SButton).IsFocusable(false).ContentPadding(FMargin(2.f, 1.f)).HAlign(HAlign_Center)
                    .ToolTipText(NSLOCTEXT("ItemQuickSlots", "ThresholdHelp", "클릭하여 자동 회복 HP 기준을 25% / 50% / 75%로 변경합니다."))
                    .OnClicked_Lambda([Slots] { if (Slots.IsValid()) Slots->CyclePotionHealthThreshold(); return FReply::Handled(); })
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).MinDesiredWidth(60.f).Justification(ETextJustify::Center)
                        .Text_Lambda([Slots] { return FText::Format(NSLOCTEXT("ItemQuickSlots", "Threshold", "HP {0}%"),
                            FText::AsNumber(Slots.IsValid() ? Slots->GetPotionHealthThreshold() : 50)); })]]]]
        + SHorizontalBox::Slot().AutoWidth()[Grid];
}

bool URPGItemQuickSlotBar::IsMountCollectionOpen() const
{
    return MountCollection && MountCollection->IsInViewport();
}

void URPGItemQuickSlotBar::ToggleMountCollection()
{
    if (IsMountCollectionOpen()) MountCollection->CloseScreen();
    else OpenMountCollection();
}

void URPGItemQuickSlotBar::OpenMountCollection()
{
    auto* PC = GetOwningPlayer();
    if (!PC || IsMountCollectionOpen() || !RPGMountQuickSlots::FindSession(PC)) return;
    MountCollection = CreateWidget<URPGRidingCollectionWidget>(PC);
    if (!MountCollection) return;

    const bool bRestoreGameInput = !PC->ShouldShowMouseCursor();
    MountCollection->SetGameplayQuickSlots(this);
    MountCollection->OnClosed.AddWeakLambda(this, [this, bRestoreGameInput]
    {
        MountCollection = nullptr;
        if (auto* Controller = GetOwningPlayer(); Controller && bRestoreGameInput)
        {
            Controller->SetInputMode(FInputModeGameOnly());
            Controller->SetShowMouseCursor(false);
        }
    });
    MountCollection->AddToViewport(140);
    FInputModeGameAndUI Mode;
    Mode.SetWidgetToFocus(MountCollection->TakeWidget());
    Mode.SetHideCursorDuringCapture(false);
    PC->SetInputMode(Mode);
    PC->SetShowMouseCursor(true);
}

void URPGItemQuickSlotBar::NativeDestruct()
{
    if (MountCollection) MountCollection->CloseScreen();
    Super::NativeDestruct();
}
