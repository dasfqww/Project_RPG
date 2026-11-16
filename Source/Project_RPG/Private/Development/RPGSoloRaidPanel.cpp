#include "Development/RPGSoloRaidPanel.h"
#include "Development/RPGSoloRaidSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "UI/RPGWindowInputState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

namespace
{
    const FTextBlockStyle& PanelText()
    {
        static const FTextBlockStyle Style = FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
            .SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18)).SetColorAndOpacity(FLinearColor::White);
        return Style;
    }
}

void URPGSoloRaidPanel::SetWorkbench(URPGSoloRaidSubsystem* Value)
{
    Workbench = Value;
    SetIsFocusable(true);
}

TSharedRef<SWidget> URPGSoloRaidPanel::RebuildWidget()
{
    auto Column = SNew(SVerticalBox);
    const TWeakObjectPtr<URPGSoloRaidSubsystem> Owner = Workbench;
    Column->AddSlot().AutoHeight().Padding(0,0,0,8)
        [SNew(STextBlock).TextStyle(&PanelText()).Text(FText::FromString(TEXT("1인 레이드 지휘  |  F8: 닫기 / 다시 열기")))];
    Column->AddSlot().AutoHeight().Padding(0,0,0,8)
        [SNew(STextBlock).TextStyle(&PanelText()).AutoWrapText(true).Text_Lambda([Owner] { return FText::FromString(Owner.IsValid() ? Owner->GetOverview() : TEXT("종료됨")); })];
    auto Commands = SNew(SHorizontalBox);
    const TPair<const TCHAR*, const TCHAR*> Modes[] = {
        {TEXT("공격 F5"), TEXT("fight")}, {TEXT("대기 F6"), TEXT("hold")},
        {TEXT("집결 F7"), TEXT("gather")}, {TEXT("산개 F9"), TEXT("spread")}};
    for (const auto& Mode : Modes)
    {
        const FString Command = Mode.Value;
        Commands->AddSlot().FillWidth(1).Padding(2)[SNew(SButton).TextStyle(&PanelText()).IsFocusable(false)
            .Text(FText::FromString(Mode.Key)).OnClicked_Lambda([Owner, Command] {
                if (Owner.IsValid()) Owner->RequestMode(Command); return FReply::Handled(); })];
    }
    Column->AddSlot().AutoHeight()[Commands];
    for (int32 BotSlot = 0; Owner.IsValid() && BotSlot < Owner->GetPartySize(); ++BotSlot)
    {
        Column->AddSlot().AutoHeight().Padding(0,8,0,2)
            [SNew(STextBlock).TextStyle(&PanelText()).AutoWrapText(true).Text_Lambda([Owner,BotSlot] {
                return FText::FromString(Owner.IsValid() ? Owner->GetSlotStatus(BotSlot) : TEXT("종료됨")); })];
        if (BotSlot == 0 && Owner->IsHumanLeader()) continue;
        auto Policies = SNew(SHorizontalBox);
        const TPair<const TCHAR*, const TCHAR*> Options[] = {{TEXT("자동 참여"),TEXT("assist")},
            {TEXT("이 봇 대기"),TEXT("hold")}, {TEXT("기믹 무시"),TEXT("ignore-mechanic")}, {TEXT("회피 끄기"),TEXT("no-avoid")}};
        for (const auto& Option : Options)
        {
            const FString Policy = Option.Value;
            Policies->AddSlot().FillWidth(1).Padding(2)[SNew(SButton).TextStyle(&PanelText()).IsFocusable(false)
                .Text(FText::FromString(Option.Key)).IsEnabled_Lambda([Owner,BotSlot,Policy] {
                    return Owner.IsValid() && Owner->GetBotPolicy(BotSlot) != Policy; })
                .OnClicked_Lambda([Owner,BotSlot,Policy] { if (Owner.IsValid()) Owner->SetBotPolicy(BotSlot,Policy); return FReply::Handled(); })];
        }
        Column->AddSlot().AutoHeight()[Policies];
    }
    Column->AddSlot().AutoHeight().Padding(0,10)
        [SNew(STextBlock).TextStyle(&PanelText()).AutoWrapText(true).Text(FText::FromString(TEXT("F8로 닫으면 내 캐릭터를 직접 조작합니다.\n기믹 무시: 소환몹을 공격하지 않음 / 회피 끄기: 예고·장판을 피하지 않음")))];
    Column->AddSlot().AutoHeight()[SNew(SButton).TextStyle(&PanelText()).IsFocusable(false)
        .Text(FText::FromString(TEXT("현재 전투 초기화 F10 — 보스·파티 복구 후 준비 상태")))
        .OnClicked_Lambda([Owner]{ if(Owner.IsValid()) Owner->RequestMode(TEXT("restart")); return FReply::Handled(); })];
    Column->AddSlot().AutoHeight().Padding(0,4)[SNew(SButton).TextStyle(&PanelText()).IsFocusable(false)
        .Text(FText::FromString(TEXT("전투로 돌아가기 F8")))
        .OnClicked_Lambda([Owner]{ if(Owner.IsValid()) Owner->TogglePanel(); return FReply::Handled(); })];
    Column->AddSlot().AutoHeight()[SNew(SButton).TextStyle(&PanelText()).IsFocusable(false)
        .Text(FText::FromString(TEXT("테스트 종료 — 이 파티의 모든 창 정리")))
        .OnClicked_Lambda([Owner]{ if(Owner.IsValid()) Owner->RequestMode(TEXT("quit")); return FReply::Handled(); })];
    return SNew(SOverlay) + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(16)
        [SNew(SBox).WidthOverride(660).MaxDesiredHeight(960)
            [SNew(SBorder).Padding(16).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.035f,.05f,.97f))
                [SNew(SScrollBox) + SScrollBox::Slot()[Column]]]];
}

FReply URPGSoloRaidPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Workbench.IsValid() && Workbench->HandleWorkbenchKey(Event.GetKey())) return FReply::Handled();
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void URPGSoloRaidSubsystem::TogglePanel()
{
    if (!IsHumanLeader() || !bReadyReported) return;
    if (Panel && Panel->IsInViewport()) { ClosePanel(); return; }
    auto* PC = GetWorld()->GetFirstPlayerController();
    if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) return;
    Panel = CreateWidget<URPGSoloRaidPanel>(PC);
    if (!Panel) return;
    Panel->SetWorkbench(this);
    PanelInput = MakeShared<FRPGWindowInputState>();
    // Use the existing counted input-lock/focus restoration contract.
    PC->FlushPressedKeys();
    Panel->AddToPlayerScreen(250);
    PanelInput->BeginWindow(*PC, *Panel, 250);
    PanelInput->FocusWindow(*Panel);
    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_PANEL Open=1 HumanInputAutomation=0"));
}
void URPGSoloRaidSubsystem::ClosePanel()
{
    if (PanelInput) { PanelInput->Reset(); PanelInput.Reset(); }
    if (Panel) { Panel->RemoveFromParent(); Panel = nullptr; }
    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_PANEL Open=0"));
}
bool URPGSoloRaidSubsystem::HandleWorkbenchKey(const FKey& Key)
{
    if (!IsHumanLeader()) return false;
    if (Key == EKeys::F8) { TogglePanel(); return true; }
    if (Key == EKeys::F5) return RequestMode(TEXT("fight"));
    if (Key == EKeys::F6) return RequestMode(TEXT("hold"));
    if (Key == EKeys::F7) return RequestMode(TEXT("gather"));
    if (Key == EKeys::F9) return RequestMode(TEXT("spread"));
    if (Key == EKeys::F10) return RequestMode(TEXT("restart"));
    return false;
}
