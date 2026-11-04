#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/LocalPlayer.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGDialogueManager.h"
#include "Narrative/RPGQuestManager.h"

void URPGNarrativeActionWidget::Configure(const FText& Label, const FString& DialogueId,
    const FString& ChoiceId, FRPGNarrativeAction Action)
{
    Caption = Label; NodeId = DialogueId; OptionId = ChoiceId; OnAction = MoveTemp(Action);
    if (LabelWidget) LabelWidget->SetText(Caption);
}
TSharedRef<SWidget> URPGNarrativeActionWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("NarrativeActionButton"));
        LabelWidget = WidgetTree->ConstructWidget<UTextBlock>();
        LabelWidget->SetText(Caption); LabelWidget->SetAutoWrapText(true);
        Button->SetContent(LabelWidget); Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Click);
        WidgetTree->RootWidget = Button;
    }
    return Super::RebuildWidget();
}
void URPGNarrativeActionWidget::Click() { if (GetIsEnabled()) OnAction.ExecuteIfBound(NodeId, OptionId); }

TSharedRef<SWidget> URPGNarrativeDialogueWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Canvas;
        auto* Width = WidgetTree->ConstructWidget<USizeBox>(); Width->SetWidthOverride(720.f);
        auto* CanvasSlot = Canvas->AddChildToCanvas(Width); CanvasSlot->SetAnchors(FAnchors(.5f)); CanvasSlot->SetAlignment(FVector2D(.5f)); CanvasSlot->SetAutoSize(true);
        auto* Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetPadding(FMargin(24.f));
        Border->SetBrushColor(FLinearColor(.025f, .035f, .055f, .98f)); Width->SetContent(Border);
        auto* Page = WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Page);
        auto Text = [&](const TCHAR* Name)
        {
            auto* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
            Label->SetAutoWrapText(true); Page->AddChildToVerticalBox(Label)->SetPadding(FMargin(0, 5)); return Label;
        };
        Text(TEXT("NarrativeTitle"))->SetText(NSLOCTEXT("RPGNarrative", "Title", "인물과 이야기"));
        Speaker = Text(TEXT("NarrativeSpeaker")); Body = Text(TEXT("NarrativeBody"));
        Progress = Text(TEXT("NarrativeProgress")); Status = Text(TEXT("NarrativeStatus"));
        auto* JournalHeight = WidgetTree->ConstructWidget<USizeBox>(); JournalHeight->SetHeightOverride(120.f);
        Page->AddChildToVerticalBox(JournalHeight);
        auto* JournalScroll = WidgetTree->ConstructWidget<UScrollBox>(); JournalHeight->SetContent(JournalScroll);
        Journal = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NarrativeQuestJournal"));
        Journal->SetAutoWrapText(true); JournalScroll->AddChild(Journal);
        auto* Height = WidgetTree->ConstructWidget<USizeBox>(); Height->SetHeightOverride(180.f); Page->AddChildToVerticalBox(Height);
        auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Height->SetContent(Scroll);
        Actions = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NarrativeActions")); Scroll->AddChild(Actions);
        auto Button = [&](const TCHAR* Name, const FText& Label)
        {
            auto* Control = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
            auto* Caption = WidgetTree->ConstructWidget<UTextBlock>(); Caption->SetText(Label); Control->SetContent(Caption);
            Page->AddChildToVerticalBox(Control)->SetPadding(FMargin(0, 4)); return Control;
        };
        RefreshButton = Button(TEXT("NarrativeRefresh"), NSLOCTEXT("RPGNarrative", "Refresh", "대화 다시 확인"));
        RefreshButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RefreshStory);
        RetryButton = Button(TEXT("NarrativeRetry"), NSLOCTEXT("RPGNarrative", "Retry", "선택 결과 다시 확인"));
        RetryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RetryChoice);
        Button(TEXT("NarrativeClose"), NSLOCTEXT("RPGNarrative", "Close", "닫기"))->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseScreen);
    }
    return Super::RebuildWidget();
}
bool URPGNarrativeDialogueWidget::MatchesSelection() const
{
    return IsValid(Coordinator) && Character.IsValid() && Coordinator->GetSelectedCharacterId() == Character;
}
bool URPGNarrativeDialogueWidget::OpenForCharacter(const FGuid CharacterId)
{
    auto* Player = GetOwningLocalPlayer();
    auto* Flow = Player ? Player->GetSubsystem<URPGNarrativeCoordinator>() : nullptr;
    if (!CharacterId.IsValid() || !Flow) return false;
    if (Flow->GetSelectedCharacterId() != CharacterId && !Flow->SelectCharacter(CharacterId)) return false;
    Unbind(); Coordinator = Flow; Character = CharacterId; FailureText = FText::GetEmpty();
    Coordinator->OnChanged.AddUniqueDynamic(this, &ThisClass::HandleChanged);
    Coordinator->OnRequestFinished.AddUniqueDynamic(this, &ThisClass::HandleFinished);
    if (!Flow->IsBusy() && !Flow->HasPendingChoice()) Flow->Refresh();
    Render(); return true;
}
void URPGNarrativeDialogueWidget::NativeConstruct() { Super::NativeConstruct(); Render(); }
void URPGNarrativeDialogueWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    const bool bReadable = MatchesSelection() && Coordinator->HasCurrentSnapshot();
    const bool bBusy = MatchesSelection() && Coordinator->IsBusy();
    const bool bPending = MatchesSelection() && Coordinator->HasPendingChoice();
    if (bReadable != bLastReadable || bBusy != bLastBusy || bPending != bLastPending) Render();
}
void URPGNarrativeDialogueWidget::Unbind()
{
    if (Coordinator)
    {
        Coordinator->OnChanged.RemoveDynamic(this, &ThisClass::HandleChanged);
        Coordinator->OnRequestFinished.RemoveDynamic(this, &ThisClass::HandleFinished);
    }
}
void URPGNarrativeDialogueWidget::NativeDestruct()
{
    Unbind();
    FRPGDialogueNode Active;
    auto* Dialogue = MatchesSelection() ? Coordinator->GetDialogueManager() : nullptr;
    if (Dialogue && Dialogue->GetActiveDialogue(Active) && Active.Id == OwnedDialogue)
        Coordinator->CloseDialogue();
    Coordinator = nullptr; OwnedDialogue.Reset(); Super::NativeDestruct();
}
void URPGNarrativeDialogueWidget::CloseScreen()
{
    Unbind();
    FRPGDialogueNode Active;
    auto* Dialogue = MatchesSelection() ? Coordinator->GetDialogueManager() : nullptr;
    if (Dialogue && Dialogue->GetActiveDialogue(Active) && Active.Id == OwnedDialogue) Coordinator->CloseDialogue();
    OwnedDialogue.Reset(); RemoveFromParent();
}
FText URPGNarrativeDialogueWidget::GetDisplayedStatus() const { return Status ? Status->GetText() : FText::GetEmpty(); }
void URPGNarrativeDialogueWidget::RefreshStory()
{
    if (MatchesSelection()) { FailureText = FText::GetEmpty(); Coordinator->Refresh(); Render(); }
}
void URPGNarrativeDialogueWidget::RetryChoice()
{
    if (MatchesSelection()) { FailureText = FText::GetEmpty(); Coordinator->RetryPendingChoice(); Render(); }
}
void URPGNarrativeDialogueWidget::HandleChanged() { Render(); }
void URPGNarrativeDialogueWidget::HandleFinished(bool bSuccess, const FString&, bool bOutcomeUnknown)
{
    FailureText = bSuccess ? FText::GetEmpty() : bOutcomeUnknown
        ? NSLOCTEXT("RPGNarrative", "Unknown", "선택 결과를 아직 확인하지 못했습니다. 다시 확인해 주세요.")
        : NSLOCTEXT("RPGNarrative", "Failed", "대화를 확인하지 못했습니다. 연결 상태를 확인하고 다시 시도해 주세요.");
    Render();
}
void URPGNarrativeDialogueWidget::Act(const FString& DialogueId, const FString& ChoiceId)
{
    if (!GetIsEnabled() || !MatchesSelection() || !Coordinator->HasCurrentSnapshot() || Coordinator->IsBusy() || Coordinator->HasPendingChoice()) return;
    if (ChoiceId.IsEmpty())
    {
        if (Coordinator->BeginDialogue(DialogueId)) OwnedDialogue = DialogueId;
    }
    else
    {
        FRPGDialogueNode Active;
        if (Coordinator->GetDialogueManager()->GetActiveDialogue(Active) && Active.Id == DialogueId)
        {
            if (Coordinator->Choose(ChoiceId))
            {
                FRPGDialogueNode Current;
                OwnedDialogue = Coordinator->GetDialogueManager()->GetActiveDialogue(Current) ? Current.Id : FString();
            }
        }
    }
    Render();
}
void URPGNarrativeDialogueWidget::AddAction(const FText& Label, const FString& DialogueId, const FString& ChoiceId, bool bEnabled)
{
    auto* Row = CreateWidget<URPGNarrativeActionWidget>(this, URPGNarrativeActionWidget::StaticClass());
    if (!Row) return;
    Row->Configure(Label, DialogueId, ChoiceId, FRPGNarrativeAction::CreateUObject(this, &ThisClass::Act));
    Row->SetIsEnabled(bEnabled); Actions->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 5));
}
void URPGNarrativeDialogueWidget::Render()
{
    if (!Actions || !Speaker || !Body || !Status || !Journal) return;
    bLastReadable = MatchesSelection() && Coordinator->HasCurrentSnapshot();
    bLastBusy = MatchesSelection() && Coordinator->IsBusy();
    bLastPending = MatchesSelection() && Coordinator->HasPendingChoice();
    Actions->ClearChildren(); Speaker->SetText(FText::GetEmpty()); Body->SetText(FText::GetEmpty()); Progress->SetText(FText::GetEmpty());
    Journal->SetText(FText::GetEmpty());
    RefreshButton->SetIsEnabled(MatchesSelection() && !bLastBusy && !bLastPending);
    RetryButton->SetVisibility(bLastPending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RetryButton->SetIsEnabled(!bLastBusy);
    if (bLastReadable)
    {
        const auto Quests = Coordinator->GetQuestManager()->GetQuests(Character);
        const int32 Done = Quests.FilterByPredicate([](const auto& Q) { return Q.Status == ERPGQuestStatus::Completed; }).Num();
        Progress->SetText(FText::Format(NSLOCTEXT("RPGNarrative", "Progress", "완료한 의뢰 {0}개"), FText::AsNumber(Done)));
        TArray<FText> Lines;
        for (const auto& Quest : Quests)
        {
            const FText Title = Quest.Title.IsEmpty() ? NSLOCTEXT("RPGNarrative", "UnnamedQuest", "의뢰") : Quest.Title;
            const FText State = Quest.Status == ERPGQuestStatus::Completed ? NSLOCTEXT("RPGNarrative", "CompletedQuest", "완료")
                : Quest.Status == ERPGQuestStatus::ReadyToReport ? NSLOCTEXT("RPGNarrative", "ReportableQuest", "보고 가능")
                : NSLOCTEXT("RPGNarrative", "ActiveQuest", "진행 중");
            Lines.Add(FText::Format(NSLOCTEXT("RPGNarrative", "JournalHeading", "{0} · {1}"), Title, State));
            if (Quest.Status == ERPGQuestStatus::Completed) continue;
            if (Quest.Objectives.IsEmpty())
                Lines.Add(FText::Format(NSLOCTEXT("RPGNarrative", "LegacyGoals", "  완료한 목표 {0}개"), FText::AsNumber(Quest.CompletedObjectives.Num())));
            for (int32 Index = 0; Index < Quest.Objectives.Num(); ++Index)
            {
                const auto& Objective = Quest.Objectives[Index];
                const FText Label = Objective.Text.IsEmpty() ? FText::Format(NSLOCTEXT("RPGNarrative", "UnnamedObjective", "목표 {0}"), FText::AsNumber(Index + 1)) : Objective.Text;
                Lines.Add(FText::Format(NSLOCTEXT("RPGNarrative", "JournalGoal", "  {0} {1}"),
                    Objective.bCompleted ? NSLOCTEXT("RPGNarrative", "GoalDone", "[완료]") : FText::FromString(TEXT("[ ]")), Label));
            }
        }
        Journal->SetText(Lines.IsEmpty() ? NSLOCTEXT("RPGNarrative", "NoQuests", "수락한 의뢰가 없습니다.") : FText::Join(FText::FromString(TEXT("\n")), Lines));
        FRPGDialogueNode Active;
        if (Coordinator->GetDialogueManager()->GetActiveDialogue(Active))
        {
            Speaker->SetText(Active.Speaker); Body->SetText(Active.Text);
            for (const auto& Choice : Active.Choices) AddAction(Choice.Text, Active.Id, Choice.Id, !bLastBusy && !bLastPending);
        }
        else
        {
            const auto Nodes = Coordinator->GetDialogueManager()->GetAvailableDialogues();
            for (const auto& Node : Nodes)
                if (Node.bEntryPoint)
                    AddAction(FText::Format(NSLOCTEXT("RPGNarrative", "Node", "{0}: {1}"), Node.Speaker, Node.Text), Node.Id, FString(), !bLastBusy && !bLastPending);
            if (Nodes.IsEmpty()) Body->SetText(NSLOCTEXT("RPGNarrative", "Empty", "지금 진행할 수 있는 대화가 없습니다."));
        }
    }
    Status->SetText(bLastBusy ? NSLOCTEXT("RPGNarrative", "Busy", "이야기를 확인하고 있습니다…") : !FailureText.IsEmpty() ? FailureText
        : bLastPending ? NSLOCTEXT("RPGNarrative", "Pending", "선택 결과를 다시 확인해 주세요.")
        : !bLastReadable ? NSLOCTEXT("RPGNarrative", "Load", "대화를 불러오려면 다시 확인해 주세요.") : FText::GetEmpty());
}
