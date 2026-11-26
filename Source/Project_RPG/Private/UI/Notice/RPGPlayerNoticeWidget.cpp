#include "UI/Notice/RPGPlayerNoticeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

void URPGPlayerNoticeWidget::UnbindSource()
{
    if (auto* UI = Source.Get()) UI->OnNoticeTextChanged.RemoveDynamic(this, &ThisClass::OnNoticeReceived);
    if (auto* Actor = SourceActor.Get()) Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::OnSourceEndPlay);
    Source.Reset(); SourceActor.Reset();
}

void URPGPlayerNoticeWidget::SetNoticeSource(UPlayerUIComponent* Next)
{
    UnbindSource();
    Source = Next;
    if (bConstructed && IsValid(Next) && Next->IsRegistered() && IsValid(Next->GetOwner())
        && !Next->GetOwner()->IsActorBeingDestroyed())
    {
        Next->OnNoticeTextChanged.AddUniqueDynamic(this, &ThisClass::OnNoticeReceived);
        SourceActor = Next->GetOwner();
        SourceActor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnSourceEndPlay);
    }
    ClearNotice();
}

void URPGPlayerNoticeWidget::OnNoticeReceived(FText Text)
{
    if (auto* UI = Source.Get(); bConstructed && UI && UI->IsRegistered()) ShowNotice(Text);
}

void URPGPlayerNoticeWidget::ShowNotice(const FText& Text)
{
    SetDisplayedNotice(Text, true);
}

void URPGPlayerNoticeWidget::ShowPersistentNotice(const FText& Text)
{
    SetDisplayedNotice(Text, false);
}

void URPGPlayerNoticeWidget::SetDisplayedNotice(const FText& Text, bool bTimed)
{
    const uint64 Current = ++Revision;
    UWorld* World = GetWorld();
    if (World) World->GetTimerManager().ClearTimer(ExpiryTimer);
    if (!bConstructed || Text.IsEmptyOrWhitespace() || !World
        || (bTimed && (!FMath::IsFinite(DisplayDuration) || DisplayDuration <= 0.f)))
    {
        DisplayedNotice = FText::GetEmpty();
        Present();
        return;
    }
    DisplayedNotice = Text;
    if (bTimed)
    {
        World->GetTimerManager().SetTimer(ExpiryTimer, FTimerDelegate::CreateWeakLambda(this, [this, Current]()
        {
            if (Revision == Current) ClearNotice();
        }), DisplayDuration, false);
    }
    Present();
}

void URPGPlayerNoticeWidget::ClearNotice()
{
    ++Revision;
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ExpiryTimer);
    DisplayedNotice = FText::GetEmpty();
    Present();
}

void URPGPlayerNoticeWidget::Present()
{
    if (NoticeText) NoticeText->SetText(DisplayedNotice);
    SetVisibility(bConstructed && !DisplayedNotice.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (bConstructed) OnNoticePresentationChanged();
}

void URPGPlayerNoticeWidget::UnbindController()
{
    if (auto* PC = BoundController.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnOwningPawnChanged);
    BoundController.Reset();
}

void URPGPlayerNoticeWidget::NativeConstruct()
{
    Super::NativeConstruct();
    bConstructed = true;
    UnbindController();
    if (auto* PC = GetOwningPlayer(); bAutoBindOwningPawn && PC && PC->IsLocalController())
    {
        BoundController = PC;
        PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnOwningPawnChanged);
        OnOwningPawnChanged(nullptr, PC->GetPawn());
    }
    else SetNoticeSource(Source.Get());
}

void URPGPlayerNoticeWidget::OnOwningPawnChanged(APawn*, APawn*)
{
    auto* PC = BoundController.Get();
    if (PC != GetOwningPlayer()) return;
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    SetNoticeSource(IsValid(Pawn) ? Pawn->FindComponentByClass<UPlayerUIComponent>() : nullptr);
}

void URPGPlayerNoticeWidget::OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type)
{
    if (Actor == SourceActor.Get()) SetNoticeSource(nullptr);
}

void URPGPlayerNoticeWidget::NativeDestruct()
{
    bConstructed = false;
    UnbindController(); UnbindSource(); ClearNotice();
    Super::NativeDestruct();
}

void URPGPlayerNoticeWidget::BeginDestroy()
{
    bConstructed = false;
    UnbindController(); UnbindSource();
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ExpiryTimer);
    Super::BeginDestroy();
}

TSharedRef<SWidget> URPGPlayerNoticeWidget::RebuildWidget()
{
    SetIsFocusable(false);
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        NoticeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NoticeText"));
        WidgetTree->RootWidget = NoticeText;
    }
    return Super::RebuildWidget();
}
