#include "UI/Narrative/RPGNarrativeTranscriptPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"

void URPGNarrativeTranscriptPanel::Configure(TSharedPtr<FRPGNarrativeTranscript> InModel, FGuid InCharacter)
{ Model = MoveTemp(InModel); Character = InCharacter; bOpen = false; Render(); }
TSharedRef<SWidget> URPGNarrativeTranscriptPanel::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Page = WidgetTree->ConstructWidget<UVerticalBox>(); WidgetTree->RootWidget = Page;
        auto Button = [&](const TCHAR* Name, const FText& Label)
        {
            auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
            auto* T = WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(Label); B->SetContent(T); Page->AddChildToVerticalBox(B); return B;
        };
        Button(TEXT("OpenTranscript"), NSLOCTEXT("RPGNarrative", "OpenTranscript", "읽은 대사 보기 (이번 접속)"))->OnClicked.AddUniqueDynamic(this, &ThisClass::OpenLog);
        LogBox = WidgetTree->ConstructWidget<USizeBox>(); LogBox->SetHeightOverride(140.f); Page->AddChildToVerticalBox(LogBox);
        auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); LogBox->SetContent(Scroll);
        LogText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogueTranscriptText")); LogText->SetAutoWrapText(true); Scroll->AddChild(LogText);
        Button(TEXT("CloseTranscript"), NSLOCTEXT("RPGNarrative", "CloseTranscript", "대사 기록 접기"))->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseLog);
        ClearButton = Button(TEXT("ClearTranscript"), NSLOCTEXT("RPGNarrative", "ClearTranscript", "이 캐릭터의 대사 기록 지우기"));
        ClearButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ClearLog);
    }
    Render(); return Super::RebuildWidget();
}
void URPGNarrativeTranscriptPanel::Render()
{
    if (!LogText) return;
    TArray<FRPGNarrativeTranscriptLine> Lines;
    const bool Valid = Model && Model->Read(Character, Lines);
    if (!Valid) bOpen = false;
    LogBox->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    ClearButton->SetIsEnabled(Valid && !Lines.IsEmpty());
    TArray<FText> Text;
    if (bOpen)
        for (const auto& Line : Lines) Text.Add(FText::Format(NSLOCTEXT("RPGNarrative", "TranscriptLine", "{0}: {1}"), Line.Speaker, Line.Text));
    LogText->SetText(!bOpen ? FText::GetEmpty() : Text.IsEmpty() ? NSLOCTEXT("RPGNarrative", "TranscriptEmpty", "이번 접속에서 읽은 대사가 없습니다.") : FText::Join(FText::FromString(TEXT("\n\n")), Text));
}
void URPGNarrativeTranscriptPanel::OpenLog() { if (GetIsEnabled()) { bOpen = true; Render(); } }
void URPGNarrativeTranscriptPanel::CloseLog() { bOpen = false; Render(); }
void URPGNarrativeTranscriptPanel::ClearLog() { if (GetIsEnabled() && Model) Model->Clear(Character); Render(); }
FText URPGNarrativeTranscriptPanel::GetDisplayedTranscript() { Render(); return LogText ? LogText->GetText() : FText::GetEmpty(); }
void URPGNarrativeTranscriptPanel::NativeTick(const FGeometry& Geometry, float DeltaTime) { Super::NativeTick(Geometry, DeltaTime); Render(); }
void URPGNarrativeTranscriptPanel::NativeDestruct() { bOpen = false; Model.Reset(); Super::NativeDestruct(); }
