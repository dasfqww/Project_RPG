#include "Actor/Narrative/RPGChapterOneClue.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "Narrative/RPGQuestManager.h"
#include "Player/RPGPlayerState.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "EngineUtils.h"
#include "UI/HUD/RPGHUDLayout.h"
#include "Component/UI/PlayerUIComponent.h"

TSharedRef<SWidget> URPGChapterOneClueLabel::RebuildWidget()
{
    if (!WidgetTree->RootWidget)
    {
        Caption = WidgetTree->ConstructWidget<UTextBlock>();
        Caption->SetWrapTextAt(260.f);
        auto Font = Caption->GetFont(); Font.Size = 12; Caption->SetFont(Font);
        Caption->SetJustification(ETextJustify::Center);
        Caption->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        Caption->SetShadowOffset(FVector2D(2,2));
        Caption->SetShadowColorAndOpacity(FLinearColor::Black);
        WidgetTree->RootWidget = Caption;
    }
    Caption->SetText(PendingCaption);
    return Super::RebuildWidget();
}
void URPGChapterOneClueLabel::SetCaption(const FText& Value)
{
    PendingCaption = Value;
    if (Caption) Caption->SetText(Value);
}
ARPGChapterOneClue::ARPGChapterOneClue()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .25f;
    Label = CreateDefaultSubobject<UWidgetComponent>(TEXT("PersonalClueLabel"));
    Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,0,140));
    Label->SetWidgetSpace(EWidgetSpace::Screen);
    Label->SetDrawAtDesiredSize(true);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGChapterOneClue::ConfigureClue()
{
    if (ClueNumber < 1 || ClueNumber > 3) { TargetId.Reset(); return; }
    TargetId = FString::Printf(TEXT("CH01.CL%02d"), ClueNumber);
    const FText Names[] = {
        NSLOCTEXT("ChapterOne", "Cart", "화물이 남은 수레"),
        NSLOCTEXT("ChapterOne", "Brace", "안쪽 버팀대"),
        NSLOCTEXT("ChapterOne", "Signal", "점검 통로의 생존 신호")};
    DisplayName = Names[ClueNumber-1];
}
void ARPGChapterOneClue::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform); ConfigureClue();
}
void ARPGChapterOneClue::BeginPlay()
{
    ConfigureClue(); Super::BeginPlay();
    if (Nameplate) Nameplate->SetVisibility(false);
    if (GetNetMode() == NM_DedicatedServer) SetActorTickEnabled(false);
}
void ARPGChapterOneClue::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode() == NM_DedicatedServer || ClueNumber < 1 || ClueNumber > 3) return;
    auto* PC = GetWorld()->GetFirstPlayerController();
    auto* Local = PC && PC->IsLocalController() ? PC->GetLocalPlayer() : nullptr;
    if (!Local) { Label->SetVisibility(false); return; }
    if (!PersonalLabel || PersonalLabel->GetOwningPlayer() != PC)
    {
        PersonalLabel = CreateWidget<URPGChapterOneClueLabel>(PC);
        Label->SetOwnerPlayer(Local); Label->SetWidget(PersonalLabel);
    }
    if (!PersonalLabel) return;
    const APawn* Pawn = PC->GetPawn();
    // Only the nearest nearby clue owns a world caption. Never clamp several
    // long screen-space captions onto a HUD edge or over the fixed side cards.
    bool bShowCaption = Pawn && FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) < FMath::Square(600.f);
    const auto* UI = Pawn ? Pawn->FindComponentByClass<UPlayerUIComponent>() : nullptr;
    bShowCaption = bShowCaption && (!UI || !UI->GetActionProgressSnapshot().bActive);
    if (bShowCaption)
    {
        const double Distance = FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation());
        for (TActorIterator<ARPGChapterOneClue> It(GetWorld()); It; ++It)
            if (*It != this && !It->IsActorBeingDestroyed() && It->ClueNumber >= 1 && It->ClueNumber <= 3)
            {
                const double OtherDistance = FVector::DistSquared(Pawn->GetActorLocation(), It->GetActorLocation());
                if (OtherDistance < Distance || (OtherDistance == Distance && It->GetUniqueID() < GetUniqueID()))
                    { bShowCaption = false; break; }
            }
        FVector2D Point;
        int32 X = 0, Y = 0; PC->GetViewportSize(X, Y);
        const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
        bShowCaption = bShowCaption && Scale > 0.f
            && PC->ProjectWorldLocationToScreen(Label->GetComponentLocation(), Point, true)
            && RPGHUDLayout::IsWorldLabelInSafeArea(Point / Scale, FVector2D(X * Local->Size.X, Y * Local->Size.Y) / Scale);
    }
    Label->SetVisibility(bShowCaption);
    FString Text = DisplayName.ToString();
    auto* Flow = Local->GetSubsystem<URPGNarrativeCoordinator>();
    auto* State = PC->GetPlayerState<ARPGPlayerState>(); FGuid Character;
    if (Flow && State && FGuid::Parse(State->GetBackendCharacterId(), Character)
        && Flow->GetSelectedCharacterId() == Character && Flow->HasCurrentSnapshot())
    {
        const auto Quests = Flow->GetQuestManager()->GetQuests(Character);
        const auto* Quest = Quests.FindByPredicate([](const auto& Q){return Q.QuestId == TEXT("CH01.M03");});
        const FString Ids[] = {TEXT("cart"),TEXT("brace"),TEXT("signal")};
        const FText Facts[] = {
            NSLOCTEXT("ChapterOne", "CartFact", "짐은 그대로다. 사람들은 갱도 쪽으로 움직였다."),
            NSLOCTEXT("ChapterOne", "BraceFact", "버팀대는 안쪽에서 걸었다. 누군가 일부러 길을 막았다."),
            NSLOCTEXT("ChapterOne", "SignalFact", "똑… 똑… 안쪽에서 응답이 돌아온다. 사람이 남아 있는 것 같다.")};
        if (Quest && Quest->CompletedObjectives.Contains(Ids[ClueNumber-1]))
            Text += TEXT("\n") + Facts[ClueNumber-1].ToString();
        else if (Quest && (ClueNumber == 1 || Quest->CompletedObjectives.Contains(Ids[ClueNumber-2])))
            Text += TEXT("\n") + NSLOCTEXT("ChapterOne", "CurrentClue", "[현재 목표] 가까이에서 조사하기").ToString();
        else Text += TEXT("\n") + NSLOCTEXT("ChapterOne", "NotActive", "수락한 의뢰와 앞선 단서를 확인하세요").ToString();
        if (Quest && Quest->Status == ERPGQuestStatus::Completed && ClueNumber == 3)
        {
            const auto* Approach = Quests.FindByPredicate([](const auto& Q){return Q.QuestId == TEXT("CH01.M04");});
            if (!Approach)
                Text += TEXT("\n") + NSLOCTEXT("ChapterOne", "NextQuest", "접근로 확보 — 옆의 현장 목표에서 다음 의뢰 수락").ToString();
            else if (Approach->Status == ERPGQuestStatus::Completed)
                Text += TEXT("\n") + NSLOCTEXT("ChapterOne", "ApproachSaved", "[의뢰 완료] 접근로 확보 — 입문 1 실적 저장 확인").ToString();
            else if (Approach->Status == ERPGQuestStatus::Active)
            {
                const auto* Objectives = Pawn ? Pawn->FindComponentByClass<URPGIntroApproachObjectiveComponent>() : nullptr;
                const auto Progress = Objectives ? Objectives->GetApproachProgress() : FRPGIntroApproachProgress{};
                Text += TEXT("\n") + (Progress.IsComplete()
                    ? NSLOCTEXT("ChapterOne", "ApproachPending", "[접근로 확보] 개인 실습 완료 · 파티 완료와 정산 저장 확인 중")
                    : NSLOCTEXT("ChapterOne", "ApproachPractice", "[접근로 확보] 북쪽 실습장 01 출발 → 02 돌진 접근 → 03 회전 베기\n회전 베기는 로비에서 편성·저장하세요. 현재 입력과 실습 진도는 화면의 접근 실습 안내를 확인하세요.")).ToString();
                if (ApproachTrackingOfferedFor != Character)
                {
                    ApproachTrackingOfferedFor = Character;
                    FRPGQuestProgress Tracked;
                    auto Tracking = Flow->GetQuestTracking();
                    if (Tracking && !Tracking->Read(Tracked)) Tracking->Track(Character, TEXT("CH01.M04"));
                }
            }
        }
        if (Quest && Quest->Status == ERPGQuestStatus::Active && TrackingOfferedFor != Character)
        {
            TrackingOfferedFor = Character;
            FRPGQuestProgress Tracked;
            auto Tracking = Flow->GetQuestTracking();
            if (Tracking && !Tracking->Read(Tracked)) Tracking->Track(Character, TEXT("CH01.M03"));
        }
    }
    PersonalLabel->SetCaption(FText::FromString(Text));
}
