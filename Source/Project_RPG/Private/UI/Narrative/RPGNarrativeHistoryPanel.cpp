#include "UI/Narrative/RPGNarrativeHistoryPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> URPGNarrativeHistoryPanel::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Page = WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget = Page;
        auto* Title = WidgetTree->ConstructWidget<UTextBlock>(); Title->SetText(NSLOCTEXT("RPGNarrative", "AccountHistoryTitle", "계정 이야기 완료 이력"));
        Page->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 12, 0, 4));
        Summary = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AccountHistorySummary"));
        Summary->SetAutoWrapText(true); Page->AddChildToVerticalBox(Summary);
        RefreshButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("AccountHistoryRefresh"));
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(NSLOCTEXT("RPGNarrative", "AccountHistoryRefresh", "완료 이력 다시 확인"));
        RefreshButton->SetContent(Label); RefreshButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RefreshHistory);
        Page->AddChildToVerticalBox(RefreshButton)->SetPadding(FMargin(0, 6));
    }
    Render(); return Super::RebuildWidget();
}
void URPGNarrativeHistoryPanel::Configure(TSharedPtr<FRPGNarrativeHistoryClient> InClient, const FGuid InCharacter)
{
    Unbind(); Client = MoveTemp(InClient); Character = InCharacter;
    if (Client)
    {
        const TWeakObjectPtr<ThisClass> WeakThis(this);
        Changed = Client->OnChanged.AddLambda([WeakThis] { if (auto* Panel = WeakThis.Get()) Panel->Render(); });
    }
    Render();
}
void URPGNarrativeHistoryPanel::RefreshHistory() { if (Client) Client->Refresh(Character); Render(); }
void URPGNarrativeHistoryPanel::Unbind()
{
    if (Client) Client->OnChanged.Remove(Changed);
    Changed.Reset();
}
void URPGNarrativeHistoryPanel::NativeDestruct() { Unbind(); Client.Reset(); Super::NativeDestruct(); }
void URPGNarrativeHistoryPanel::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    const auto State = Client ? Client->GetState(Character) : ERPGNarrativeHistoryState::Idle;
    const bool Selected = Client && Client->IsSelected(Character);
    if (State != LastState || Selected != bLastSelected) Render();
}
void URPGNarrativeHistoryPanel::Render()
{
    LastState = Client ? Client->GetState(Character) : ERPGNarrativeHistoryState::Idle;
    bLastSelected = Client && Client->IsSelected(Character);
    if (!Summary || !RefreshButton) return;
    RefreshButton->SetIsEnabled(bLastSelected && LastState != ERPGNarrativeHistoryState::Loading);
    FText Text = NSLOCTEXT("RPGNarrative", "AccountHistoryIdle", "캐릭터를 선택하고 완료 이력을 확인해 주세요.");
    if (bLastSelected) Text = NSLOCTEXT("RPGNarrative", "AccountHistoryAvailable", "완료 이력을 확인해 주세요.");
    if (LastState == ERPGNarrativeHistoryState::Loading) Text = NSLOCTEXT("RPGNarrative", "AccountHistoryLoading", "완료 이력을 확인하고 있습니다…");
    else if (LastState == ERPGNarrativeHistoryState::Failed) Text = NSLOCTEXT("RPGNarrative", "AccountHistoryFailed", "완료 이력을 불러오지 못했습니다. 다시 확인해 주세요.");
    else if (Client)
        if (const auto* View = Client->GetView(Character))
        {
            TArray<FText> Lines;
            for (int32 I = 0; I < View->Chapters.Num(); ++I)
            {
                const auto& Chapter = View->Chapters[I];
                const FText Title = Chapter.Title.IsEmpty() ? FText::Format(NSLOCTEXT("RPGNarrative", "AccountHistoryUnnamed", "제목 없는 이야기 {0}"), FText::AsNumber(I + 1)) : Chapter.Title;
                const FText Owners = Chapter.bSelected && Chapter.bOther ? NSLOCTEXT("RPGNarrative", "AccountHistoryBoth", "이 캐릭터 · 다른 캐릭터 완료")
                    : Chapter.bSelected ? NSLOCTEXT("RPGNarrative", "AccountHistorySelected", "이 캐릭터 완료") : NSLOCTEXT("RPGNarrative", "AccountHistoryOther", "다른 캐릭터 완료");
                Lines.Add(FText::Format(NSLOCTEXT("RPGNarrative", "AccountHistoryLine", "{0} · {1}"), Title, Owners));
            }
            Text = Lines.IsEmpty() ? NSLOCTEXT("RPGNarrative", "AccountHistoryEmpty", "아직 보고 완료한 이야기가 없습니다.")
                : FText::Join(FText::FromString(TEXT("\n")), Lines);
            Text = FText::Format(NSLOCTEXT("RPGNarrative", "AccountHistoryReadOnly", "{0}\n완료 이력 조회는 현재 캐릭터의 진도를 바꾸지 않습니다."), Text);
        }
    Summary->SetText(Text);
}
FText URPGNarrativeHistoryPanel::GetDisplayedHistory() const { return Summary ? Summary->GetText() : FText::GetEmpty(); }
