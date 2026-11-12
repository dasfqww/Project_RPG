#include "UI/Narrative/RPGNarrativeStorySkipPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> URPGNarrativeStorySkipPanel::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Page = WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget = Page;
        auto* Title = WidgetTree->ConstructWidget<UTextBlock>();
        Title->SetText(NSLOCTEXT("RPGNarrative", "SkipTitle", "부캐 이야기 건너뛰기")); Page->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 12, 0, 4));
        Chapters = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("StorySkipChapters")); Page->AddChildToVerticalBox(Chapters);
        Summary = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StorySkipSummary"));
        Summary->SetAutoWrapText(true); Page->AddChildToVerticalBox(Summary);
        auto Button = [&](const TCHAR* Name, const FText& Label)
        {
            auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
            auto* T = WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(Label); B->SetContent(T);
            Page->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 4)); return B;
        };
        PreviewButton = Button(TEXT("StorySkipPreview"), NSLOCTEXT("RPGNarrative", "SkipPreview", "건너뛸 내용 미리보기"));
        ConfirmButton = Button(TEXT("StorySkipConfirm"), NSLOCTEXT("RPGNarrative", "SkipConfirm", "이 내용으로 건너뛰기"));
        CancelButton = Button(TEXT("StorySkipCancel"), NSLOCTEXT("RPGNarrative", "SkipCancel", "미리보기 닫기"));
        RetryButton = Button(TEXT("StorySkipRetry"), NSLOCTEXT("RPGNarrative", "SkipRetry", "스킵 결과 다시 확인"));
        PreviewButton->OnClicked.AddUniqueDynamic(this, &ThisClass::PreviewChapter);
        ConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ConfirmChapter);
        CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CancelReview);
        RetryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RetryPending);
    }
    Render(); return Super::RebuildWidget();
}
void URPGNarrativeStorySkipPanel::Configure(TSharedPtr<FRPGNarrativeSkipClient> InClient,
    TSharedPtr<FRPGNarrativeHistoryClient> InHistory, const FGuid InCharacter)
{
    Unbind(); Client = MoveTemp(InClient); History = MoveTemp(InHistory); Character = InCharacter;
    const TWeakObjectPtr<ThisClass> WeakThis(this);
    if (Client) ClientChanged = Client->OnChanged.AddLambda([WeakThis] { if (auto* P = WeakThis.Get()) P->Render(); });
    if (History) HistoryChanged = History->OnChanged.AddLambda([WeakThis] { if (auto* P = WeakThis.Get()) P->Render(); });
    Render();
}
void URPGNarrativeStorySkipPanel::Unbind()
{
    if (Client) Client->OnChanged.Remove(ClientChanged);
    if (History) History->OnChanged.Remove(HistoryChanged);
    ClientChanged.Reset(); HistoryChanged.Reset();
}
void URPGNarrativeStorySkipPanel::NativeDestruct() { Unbind(); Client.Reset(); History.Reset(); Super::NativeDestruct(); }
FString URPGNarrativeStorySkipPanel::SelectedChapter() const
{
    const int32 Index = Chapters ? Chapters->GetSelectedIndex() : INDEX_NONE;
    return CandidateIds.IsValidIndex(Index) ? CandidateIds[Index] : FString();
}
void URPGNarrativeStorySkipPanel::PreviewChapter()
{
    if (!GetIsEnabled() || !Client || !History || !History->GetView(Character)) return;
    const FString Chapter = SelectedChapter();
    const auto* View = History->GetView(Character);
    if (View->Chapters.ContainsByPredicate([&](const auto& C) { return C.Id == Chapter && C.bOther && !C.bSelected; }))
        Client->Preview(Character, Chapter);
}
void URPGNarrativeStorySkipPanel::ConfirmChapter()
{
    if (!GetIsEnabled() || !Client) return;
    if (const auto* Plan = Client->GetPreview(Character); Plan && Plan->Chapter == SelectedChapter()) Client->Confirm(Character);
}
void URPGNarrativeStorySkipPanel::CancelReview() { if (Client && Client->IsSelected(Character)) Client->CancelPreview(); }
void URPGNarrativeStorySkipPanel::RetryPending() { if (GetIsEnabled() && Client) Client->Retry(Character); }
void URPGNarrativeStorySkipPanel::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    const bool Selected = Client && Client->IsSelected(Character);
    if (Selected != bLastSelected || (Client && (Client->CanStart(Character) != bLastCanStart
        || Client->GetRevision(Character) != LastRevision || (Client->GetPreview(Character) != nullptr) != bLastReview))
        || (Chapters && Chapters->GetSelectedIndex() != LastSelection)) Render();
}
void URPGNarrativeStorySkipPanel::Render()
{
    bLastSelected = Client && Client->IsSelected(Character); bLastCanStart = Client && Client->CanStart(Character);
    LastRevision = Client ? Client->GetRevision(Character) : -1;
    const auto* Plan = Client ? Client->GetPreview(Character) : nullptr; bLastReview = Plan != nullptr;
    if (!Chapters || !Summary || !ConfirmButton) return;
    const FString Previous = SelectedChapter(); CandidateIds.Reset(); Chapters->ClearOptions();
    if (bLastSelected && History)
        if (const auto* View = History->GetView(Character))
            for (const auto& C : View->Chapters)
                if (C.bOther && !C.bSelected && !Client->IsFinished(Character, C.Id))
                {
                    const int32 Index = CandidateIds.Num(); CandidateIds.Add(C.Id);
                    // Index also distinguishes authored chapters that share a display title.
                    const FText Name = C.Title.IsEmpty() ? NSLOCTEXT("RPGNarrative", "SkipUnnamed", "제목 없는 이야기") : C.Title;
                    Chapters->AddOption(FText::Format(NSLOCTEXT("RPGNarrative", "SkipOption", "{0}. {1}"), FText::AsNumber(Index + 1), Name).ToString());
                }
    const int32 PreviousIndex = CandidateIds.IndexOfByKey(Previous);
    if (!CandidateIds.IsEmpty()) Chapters->SetSelectedIndex(PreviousIndex != INDEX_NONE ? PreviousIndex : 0);
    LastSelection = Chapters->GetSelectedIndex();
    const auto State = Client ? Client->GetState(Character) : ERPGNarrativeSkipState::Idle;
    const bool Busy = Client && Client->IsBusy(), Pending = Client && Client->HasPending();
    Chapters->SetIsEnabled(bLastCanStart && !CandidateIds.IsEmpty());
    PreviewButton->SetIsEnabled(bLastCanStart && !SelectedChapter().IsEmpty());
    const bool ShowPlan = Plan && Plan->Chapter == SelectedChapter();
    ConfirmButton->SetVisibility(ShowPlan ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    ConfirmButton->SetIsEnabled(ShowPlan && bLastCanStart);
    CancelButton->SetVisibility(ShowPlan ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RetryButton->SetVisibility(bLastSelected && Pending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RetryButton->SetIsEnabled(bLastSelected && Pending && !Busy);
    FText Text = NSLOCTEXT("RPGNarrative", "SkipReadFirst", "위의 완료 이력을 확인한 뒤 챕터를 선택해 주세요. 이력만으로 실행 자격이 확정되지는 않습니다.");
    if (!bLastSelected) Text = NSLOCTEXT("RPGNarrative", "SkipWrongOwner", "현재 선택한 캐릭터의 이야기 창에서 확인해 주세요.");
    else if (State == ERPGNarrativeSkipState::Uncertain) Text = NSLOCTEXT("RPGNarrative", "SkipUnknown", "스킵 결과를 아직 확인하지 못했습니다. 결과를 다시 확인해 주세요.");
    else if (Busy) Text = NSLOCTEXT("RPGNarrative", "SkipLoading", "이야기 스킵을 확인하고 있습니다…");
    else if (State == ERPGNarrativeSkipState::Failed) Text = NSLOCTEXT("RPGNarrative", "SkipFailed", "현재 진도·접속 상태 또는 챕터 조건을 확인하지 못했습니다. 이야기를 다시 확인하고 미리보기를 요청해 주세요.");
    else if (State == ERPGNarrativeSkipState::Completed) Text = NSLOCTEXT("RPGNarrative", "SkipDone", "이야기 스킵이 반영되었습니다.");
    else if (!bLastCanStart) Text = NSLOCTEXT("RPGNarrative", "SkipWait", "먼저 이야기 진도를 확인하고, 던전 참가·진행을 마쳐 주세요.");
    else if (History && History->GetView(Character) && CandidateIds.IsEmpty()) Text = NSLOCTEXT("RPGNarrative", "SkipNoCandidates", "다른 캐릭터의 완료 이력에서 검토할 챕터가 없습니다.");
    if (ShowPlan)
    {
        TArray<FText> Lines;
        Lines.Add(Plan->Title.IsEmpty() ? NSLOCTEXT("RPGNarrative", "SkipSelectedStory", "선택한 이야기") : Plan->Title);
        Lines.Add(NSLOCTEXT("RPGNarrative", "SkipPolicy", "무료 · 스킵한 의뢰 보상 미지급\n남은 훈련·입문 해금은 직접 수행합니다. 완료한 훈련·습득 스킬은 유지됩니다.\n레벨·장비를 자동 보정하지 않습니다.\n건너뛸 남은 이야기 의뢰:"));
        for (int32 I = 0; I < Plan->Quests.Num(); ++I)
            Lines.Add(Plan->Quests[I].Title.IsEmpty() ? FText::Format(NSLOCTEXT("RPGNarrative", "SkipUnnamedQuest", "이야기 의뢰 {0}"), FText::AsNumber(I + 1)) : Plan->Quests[I].Title);
        Lines.Add(FText::Format(NSLOCTEXT("RPGNarrative", "SkipUnlocks", "새 이야기 해금 {0}개\n직접 완료한 의뢰와 서브 의뢰는 보존됩니다."), FText::AsNumber(Plan->Unlocks.Num())));
        Text = FText::Join(FText::FromString(TEXT("\n")), Lines);
    }
    Summary->SetText(Text);
}
FText URPGNarrativeStorySkipPanel::GetDisplayedPreview() const { return Summary ? Summary->GetText() : FText::GetEmpty(); }
