// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Content/RPGBossBattleInfoWidget.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossSummonObjectiveComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Character/RPGBossCharacter.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "Components/TextBlock.h"
#include "GameState/RPGBossBattleGameState.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "RPGBossBattleInfo"

void URPGBossBattleInfoWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (SummonObjectiveText || !WidgetTree) { return; }
	if (!WidgetTree->RootWidget)
	{
		// Remote clients can use the native presentation without the legacy GameMode UI Blueprint.
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;
	}

	// Construct before Slate rebuilds the tree. Existing designer labels retain their layout.
	SummonObjectiveText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SummonObjectiveText"));
	SummonObjectiveText->SetJustification(ETextJustify::Center);
	SummonObjectiveText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .85f, .25f)));
	SummonObjectiveText->SetShadowColorAndOpacity(FLinearColor::Black);
	SummonObjectiveText->SetShadowOffset(FVector2D(1.f, 1.f));
	SummonObjectiveText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
	{
		UCanvasPanelSlot* StatusSlot = Canvas->AddChildToCanvas(SummonObjectiveText);
		StatusSlot->SetAnchors(FAnchors(.5f, 0.f));
		StatusSlot->SetAlignment(FVector2D(.5f, 0.f));
		StatusSlot->SetPosition(FVector2D(0.f, 160.f));
		StatusSlot->SetAutoSize(true);
		StatusSlot->SetZOrder(10);
	}
	else
	{
		// Preserve the existing hierarchy when the authored root is not a Canvas.
		UWidget* OriginalRoot = WidgetTree->RootWidget;
		UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>();
		Overlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Overlay;
		UOverlaySlot* ContentSlot = Overlay->AddChildToOverlay(OriginalRoot);
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
		UOverlaySlot* StatusSlot = Overlay->AddChildToOverlay(SummonObjectiveText);
		StatusSlot->SetHorizontalAlignment(HAlign_Center);
		StatusSlot->SetVerticalAlignment(VAlign_Top);
		StatusSlot->SetPadding(FMargin(0.f, 160.f, 0.f, 0.f));
	}
}

void URPGBossBattleInfoWidget::NativeConstruct()
{
	Super::NativeConstruct();
	TryBindBattleGameState();
	UpdateSummonObjectiveStatus();
}

void URPGBossBattleInfoWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TryBindBattleGameState();
	UpdateSummonObjectiveStatus();
}

FText URPGBossBattleInfoWidget::GetSummonObjectiveStatusText() const
{
	const ARPGBossBattleGameState* Battle = GetWorld() ? GetWorld()->GetGameState<ARPGBossBattleGameState>() : nullptr;
	if (!IsValid(Battle)) { return FText::GetEmpty(); }
	const FRPGBossBattleSnapshot Snapshot = Battle->GetBattleSnapshot();
	const ARPGBossCharacter* Boss = Snapshot.Boss;
	if (Snapshot.BattleState != EBossBattleState::InProgress || !IsValid(Boss) || Boss->IsActorBeingDestroyed() ||
		!IsValid(Boss->BossAIComponent) || !IsValid(Boss->BossSummonObjectiveComponent)) { return FText::GetEmpty(); }
	const FRPGBossState State = Boss->BossAIComponent->GetBossState();
	const FRPGBossSummonObjectiveState Objective = Boss->BossSummonObjectiveComponent->GetObjectiveState();
	// Components can arrive in either order. Never combine progress from another phase/encounter.
	if (State.EncounterState != ERPGBossEncounterState::InCombat || State.EncounterId <= 0 || State.PhaseIndex < 0 ||
		!Objective.bActive || Objective.EncounterId != State.EncounterId || Objective.PhaseIndex != State.PhaseIndex ||
		!FMath::IsFinite(State.PhaseEndServerTime) || State.PhaseEndServerTime <= 0.0 ||
		Objective.Sequence < 0 || Objective.RequiredCount < 0 || Objective.DefeatedCount < 0 ||
		Objective.DefeatedCount > Objective.RequiredCount) { return FText::GetEmpty(); }
	const double ServerTime = Battle->GetServerWorldTimeSeconds();
	if (!FMath::IsFinite(ServerTime)) { return FText::GetEmpty(); }
	// Zero is still a display value: only the server decides expiry, completion and failure.
	const int32 Seconds = FMath::CeilToInt(FMath::Clamp(State.PhaseEndServerTime - ServerTime, 0.0, 86400.0));
	if (Objective.Sequence == 0)
	{
		return FText::Format(LOCTEXT("SummonPreparing", "소환 중  |  {0}초"), FText::AsNumber(Seconds));
	}
	if (Objective.RequiredCount == 0) { return FText::GetEmpty(); }
	return FText::Format(LOCTEXT("SummonDefeatProgress", "소환된 적을 처치하세요 ({0}/{1})  |  {2}초"),
		FText::AsNumber(Objective.DefeatedCount), FText::AsNumber(Objective.RequiredCount), FText::AsNumber(Seconds));
}

void URPGBossBattleInfoWidget::UpdateSummonObjectiveStatus()
{
	if (!SummonObjectiveText) { return; }
	const FText Status = GetSummonObjectiveStatusText();
	if (!SummonObjectiveText->GetText().EqualTo(Status)) { SummonObjectiveText->SetText(Status); }
	SummonObjectiveText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void URPGBossBattleInfoWidget::TryBindBattleGameState()
{
	ARPGBossBattleGameState* BattleGameState = GetWorld()
		? GetWorld()->GetGameState<ARPGBossBattleGameState>() : nullptr;
	if (BoundBattleGameState.Get() == BattleGameState)
	{
		return;
	}
	if (BoundBattleGameState.IsValid())
	{
		BoundBattleGameState->OnBattleSnapshotChanged.RemoveDynamic(
			this, &ThisClass::HandleBattleSnapshotChanged);
	}
	BoundBattleGameState = BattleGameState;
	if (BattleGameState)
	{
		BattleGameState->OnBattleSnapshotChanged.AddUniqueDynamic(this, &ThisClass::HandleBattleSnapshotChanged);
		HandleBattleSnapshotChanged(BattleGameState->GetBattleSnapshot());
	}
}

void URPGBossBattleInfoWidget::NativeDestruct()
{
	if (BoundBattleGameState.IsValid())
	{
		BoundBattleGameState->OnBattleSnapshotChanged.RemoveDynamic(
			this, &ThisClass::HandleBattleSnapshotChanged);
	}
	BoundBattleGameState.Reset();
	if (SummonObjectiveText)
	{
		SummonObjectiveText->SetText(FText::GetEmpty());
		SummonObjectiveText->SetVisibility(ESlateVisibility::Collapsed);
	}
	Super::NativeDestruct();
}

void URPGBossBattleInfoWidget::HandleBattleSnapshotChanged(const FRPGBossBattleSnapshot& Snapshot)
{
	UpdateSummonObjectiveStatus();
	OnBattleSnapshotUpdated(Snapshot);
}

void URPGBossBattleInfoWidget::UpdateBattleLimitTimeText(UTextBlock* TextBlock, float InRemainingTime)
{
	if (!TextBlock || !GetWorld())
	{
		return;
	}
	if (const ARPGBossBattleGameState* BattleGameState = GetWorld()->GetGameState<ARPGBossBattleGameState>())
	{
		if (BattleGameState->GetBattleSnapshot().BattleState != EBossBattleState::InProgress)
		{
			return;
		}
		InRemainingTime = BattleGameState->GetRemainingBattleTime();
	}

	FString TimeString = URPGCoreFunctionLibrary::FormatTimeToMMSS(InRemainingTime);
	TextBlock->SetText(FText::FromString(TimeString));

	//Debug::Print(TimeString);
}

void URPGBossBattleInfoWidget::OnRestart()
{
	if (ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		Controller->RequestBossBattleRestart();
	}
}

void URPGBossBattleInfoWidget::OnExit()
{

}

#undef LOCTEXT_NAMESPACE
