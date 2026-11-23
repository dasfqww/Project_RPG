#include "UI/ItemQuickSlot/RPGItemQuickSlotBar.h"
#include "UI/RPGQuickSlotWidget.h"
#include "Controller/RPGPlayerController.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Riding/RPGMountDefinition.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
class SRPGMountQuickSlotContent final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRPGMountQuickSlotContent) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&, TWeakObjectPtr<ARPGPlayerController> InController)
    {
        Controller = InController;
        ChildSlot
        [
            SNew(SOverlay)
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SAssignNew(Label, STextBlock).Text(NSLOCTEXT("RidingQuickSlot", "Call", "탈것"))
            ]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SAssignNew(Image, SImage).Visibility(EVisibility::Collapsed)
            ]
        ];
    }

    void Tick(const FGeometry&, const double, const float) override
    {
        const auto* PC = Controller.Get();
        const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        const auto* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
        const auto* Session = PS ? PS->FindComponentByClass<URPGRidingSessionComponent>() : nullptr;
        const FString CurrentMountId = Session ? Session->GetOwnerState().QuickSlotMountId : FString();
        if (CurrentMountId != MountId)
        {
            MountId = CurrentMountId;
            Load.Reset();
            Image->SetImage(nullptr);
            Image->SetVisibility(EVisibility::Collapsed);
            Icon.Reset();
            Label->SetVisibility(EVisibility::HitTestInvisible);
            if (!MountId.IsEmpty() && UAssetManager::IsInitialized())
            {
                auto& Assets = UAssetManager::Get();
                const FPrimaryAssetId Id(URPGMountDefinition::PrimaryAssetType, FName(*MountId));
                const FSoftObjectPath Path = Assets.GetPrimaryAssetPath(Id);
                if (Path.IsValid()) Load = Assets.GetStreamableManager().RequestAsyncLoad(Path);
            }
        }

        if (!Load || !Load->HasLoadCompleted()) return;
        const auto Completed = MoveTemp(Load);
        if (const auto* Definition = Cast<URPGMountDefinition>(Completed->GetLoadedAsset()))
        {
            if (Definition->MountId == MountId && !Definition->Icon.IsNull())
                Load = UAssetManager::GetStreamableManager().RequestAsyncLoad(Definition->Icon.ToSoftObjectPath());
        }
        else if (auto* Texture = Cast<UTexture2D>(Completed->GetLoadedAsset()))
        {
            Icon = FDeferredCleanupSlateBrush::CreateBrush(Texture, FVector2D(52.f, 52.f));
            Image->SetImage(Icon->GetSlateBrush());
            Image->SetVisibility(EVisibility::HitTestInvisible);
            Label->SetVisibility(EVisibility::Collapsed);
        }
    }

private:
    TWeakObjectPtr<ARPGPlayerController> Controller;
    FString MountId;
    TSharedPtr<FStreamableHandle> Load;
    TSharedPtr<FDeferredCleanupSlateBrush> Icon;
    TSharedPtr<STextBlock> Label;
    TSharedPtr<SImage> Image;
};
}

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
    const TWeakObjectPtr<ARPGPlayerController> Controller(Cast<ARPGPlayerController>(GetOwningPlayer()));
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
        + SHorizontalBox::Slot().AutoWidth()[Grid]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8.f, 0.f, 0.f, 0.f)
        [SNew(SBox).WidthOverride(64.f).HeightOverride(64.f)
            [SNew(SButton).IsFocusable(false).HAlign(HAlign_Center).VAlign(VAlign_Center)
                .IsEnabled_Lambda([Controller] { const auto* PC = Controller.Get(); return PC && PC->CanCallQuickSlotMount(); })
                .ToolTipText(NSLOCTEXT("RidingQuickSlot", "CallHelp", "탈것 보관함에서 등록한 탈것을 호출합니다."))
                .OnClicked_Lambda([Controller]
                {
                    if (auto* PC = Controller.Get()) PC->CallQuickSlotMount();
                    return FReply::Handled();
                })
                [SNew(SRPGMountQuickSlotContent, Controller)]]];

}
