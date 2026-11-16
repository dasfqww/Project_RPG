#include "UI/Narrative/RPGNarrativeQuestTrackerWidget.h"
#include "Narrative/RPGNarrativePresentation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "UI/HUD/RPGHUDLayout.h"

void URPGNarrativeQuestTrackerWidget::Configure(TSharedPtr<FRPGNarrativeQuestTracking> InModel, FGuid InCharacter, bool bInControls)
{ Model = MoveTemp(InModel); Character = InCharacter; bControls = bInControls; OptionsKey.Reset(); Render(); }
TSharedRef<SWidget> URPGNarrativeQuestTrackerWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Page = WidgetTree->ConstructWidget<UVerticalBox>();
        if (bControls)
        {
            WidgetTree->RootWidget = Page;
            Quests = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("TrackedQuestOptions")); Page->AddChildToVerticalBox(Quests);
            auto Button = [&](const TCHAR* Name, const FText& Label)
            {
                auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
                auto* T = WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(Label); B->SetContent(T); Page->AddChildToVerticalBox(B); return B;
            };
            TrackButton = Button(TEXT("TrackQuest"), NSLOCTEXT("RPGNarrative", "TrackQuest", "선택한 의뢰 추적"));
            ClearButton = Button(TEXT("UntrackQuest"), NSLOCTEXT("RPGNarrative", "UntrackQuest", "의뢰 추적 해제"));
            TrackButton->OnClicked.AddUniqueDynamic(this, &ThisClass::TrackSelected);
            ClearButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ClearTracking);
        }
        else
        {
            auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Canvas;
            auto* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TrackedQuestCard"));
            auto* Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetPadding(FMargin(12.f));
            auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
            Scroll->AddChild(Page); Border->SetContent(Scroll);
            Border->SetBrushColor(RPGHUDLayout::PanelColor()); Width->SetContent(Border);
            auto* CanvasSlot = Canvas->AddChildToCanvas(Width);
            RPGHUDLayout::PlaceRightCard(*Width, *CanvasSlot, false);
            SetVisibility(ESlateVisibility::SelfHitTestInvisible); SetIsFocusable(false);
        }
        Summary = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrackedQuestSummary"));
        Summary->SetAutoWrapText(true); Page->AddChildToVerticalBox(Summary);
        if (!bControls)
        {
            auto Font = Summary->GetFont(); Font.Size = 12; Summary->SetFont(Font);
            Summary->SetColorAndOpacity(RPGHUDLayout::HeadingColor());
        }
    }
    Render(); return Super::RebuildWidget();
}
void URPGNarrativeQuestTrackerWidget::Render()
{
    if (!Summary) return;
    FRPGNarrativeTrackingContext Current;
    const bool Valid = Model && Model->Context(Current) && (!bControls || Current.Character == Character);
    const bool Readable = Valid && Current.bReadable;
    if (bControls && Quests)
    {
        TArray<FString> Ids, Options;
        if (Readable) for (const auto& Q : Current.Quests) if (Q.Status != ERPGQuestStatus::Completed)
        {
            Ids.Add(Q.QuestId); Options.Add(FString::Printf(TEXT("%d. %s"), Ids.Num(), *FRPGNarrativePresentation::QuestHeading(Q).ToString()));
        }
        const FString Key = Valid ? Current.Character.ToString() + Current.Endpoint + Current.Account + FString::Join(Ids, TEXT("\n")) + FString::Join(Options, TEXT("\n")) : FString();
        if (!OptionsKey.Equals(Key, ESearchCase::CaseSensitive))
        {
            const int32 Index = Quests->GetSelectedIndex(); const FString Previous = CandidateIds.IsValidIndex(Index) ? CandidateIds[Index] : FString();
            Quests->ClearOptions(); CandidateIds = MoveTemp(Ids); for (const auto& Option : Options) Quests->AddOption(Option);
            const int32 Selected = CandidateIds.IndexOfByPredicate([&](const auto& Id) { return Id.Equals(Previous, ESearchCase::CaseSensitive); });
            if (!CandidateIds.IsEmpty()) Quests->SetSelectedIndex(Selected == INDEX_NONE ? 0 : Selected);
            OptionsKey = Key;
        }
        TrackButton->SetIsEnabled(Readable && !CandidateIds.IsEmpty()); Quests->SetIsEnabled(Readable);
    }
    FRPGQuestProgress Quest;
    const bool Tracked = Readable && Model->Read(Quest);
    if (ClearButton) ClearButton->SetIsEnabled(Tracked);
    FText Text;
    if (Tracked)
    {
        TArray<FText> Lines{FRPGNarrativePresentation::QuestHeading(Quest)};
        if (Quest.Status == ERPGQuestStatus::ReadyToReport)
            Lines.Add(NSLOCTEXT("RPGNarrative", "TrackerReport", "목표를 달성했습니다. 의뢰를 보고해 주세요."));
        else if (Quest.Objectives.IsEmpty())
            Lines.Add(FText::Format(NSLOCTEXT("RPGNarrative", "TrackerLegacy", "완료한 목표 {0}개"), FText::AsNumber(Quest.CompletedObjectives.Num())));
        else for (int32 I = 0; I < Quest.Objectives.Num(); ++I) Lines.Add(FRPGNarrativePresentation::ObjectiveLine(Quest.Objectives[I], I));
        Text = FText::Join(FText::FromString(TEXT("\n")), Lines);
    }
    else if (bControls) Text = Readable ? NSLOCTEXT("RPGNarrative", "TrackerEmpty", "추적할 진행 중 의뢰를 선택해 주세요.")
        : NSLOCTEXT("RPGNarrative", "TrackerUnreadable", "현재 캐릭터의 이야기 진도를 확인해 주세요.");
    Summary->SetText(Text);
    if (!bControls && WidgetTree)
        if (auto* Card = WidgetTree->FindWidget(TEXT("TrackedQuestCard")))
            Card->SetVisibility(Tracked ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}
void URPGNarrativeQuestTrackerWidget::TrackSelected()
{
    if (!GetIsEnabled() || !Model || !Quests) return;
    const int32 Index = Quests->GetSelectedIndex();
    if (CandidateIds.IsValidIndex(Index)) Model->Track(Character, CandidateIds[Index]);
    Render();
}
void URPGNarrativeQuestTrackerWidget::ClearTracking() { if (GetIsEnabled() && Model) Model->Clear(Character); Render(); }
FText URPGNarrativeQuestTrackerWidget::GetDisplayedTracking() { Render(); return Summary ? Summary->GetText() : FText::GetEmpty(); }
void URPGNarrativeQuestTrackerWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{ Super::NativeTick(Geometry, DeltaTime); Elapsed += DeltaTime; if (Elapsed >= .15f) { Elapsed = 0.f; Render(); } }
void URPGNarrativeQuestTrackerWidget::NativeDestruct() { Model.Reset(); Super::NativeDestruct(); }
