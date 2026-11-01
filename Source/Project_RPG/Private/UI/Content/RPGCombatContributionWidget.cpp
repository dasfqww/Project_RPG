#include "UI/Content/RPGCombatContributionWidget.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Combat/RPGCombatContributionComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#define LOCTEXT_NAMESPACE "RPGCombatContributionWidget"

namespace
{
	bool IsLivingActor(AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return false;
		const auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		if (ASC && ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()))
		{
			const float Health = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
			return FMath::IsFinite(Health) && Health > 0.f;
		}
		return true;
	}
	bool IsCoherentMeter(float Value, float Threshold, bool bComplete)
	{
		return FMath::IsFinite(Value) && FMath::IsFinite(Threshold)
			&& Threshold >= 0.f && Threshold <= 100000.f && Value >= 0.f && Value <= Threshold
			&& bComplete == (Threshold > 0.f && Value >= Threshold);
	}
}

TSharedRef<SWidget> URPGCombatContributionWidget::RebuildWidget()
{
	SetIsFocusable(false);
	// Keep the root ticking when the inner card is hidden, and never intercept input.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetWidthOverride(340.f);
		auto* Position = Canvas->AddChildToCanvas(Width);
		Position->SetAnchors(FAnchors(.5f, 0.f));
		Position->SetAlignment(FVector2D(.5f, 0.f));
		Position->SetPosition(FVector2D(0.f, 220.f));
		Position->SetAutoSize(true);
		Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContributionCard"));
		Card->SetBrushColor(FLinearColor(.025f, .04f, .06f, .85f));
		Card->SetPadding(FMargin(10.f));
		Card->SetToolTipText(LOCTEXT("Meaning", "선택한 대상에 누적된 기여입니다. 완료 표시는 해당 기준에 도달했음을 뜻합니다."));
		Width->SetContent(Card);
		auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		Card->SetContent(Content);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			auto* Row = WidgetTree->ConstructWidget<UVerticalBox>();
			Content->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, Index ? 8.f : 0.f, 0.f, 0.f));
			auto* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
				Index ? TEXT("PartBreakContributionText") : TEXT("StaggerContributionText"));
			auto Font = Label->GetFont(); Font.Size = 14; Label->SetFont(Font);
			Label->SetAutoWrapText(true);
			Row->AddChildToVerticalBox(Label);
			auto* Height = WidgetTree->ConstructWidget<USizeBox>();
			Height->SetHeightOverride(6.f);
			Row->AddChildToVerticalBox(Height);
			auto* Meter = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),
				Index ? TEXT("PartBreakContributionMeter") : TEXT("StaggerContributionMeter"));
			Height->SetContent(Meter);
			MeterRows.Add(Row); Labels.Add(Label); Meters.Add(Meter);
		}
		ClearPresentation();
	}
	return Super::RebuildWidget();
}

void URPGCombatContributionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bAttached = true;
	RefreshCurrentTarget();
}

void URPGCombatContributionWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (bAttached) RefreshCurrentTarget();
}

void URPGCombatContributionWidget::RefreshCurrentTarget()
{
	const auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	AActor* Target = IsValid(Controller) && Controller->IsLocalController()
		&& Controller->GetWorld() == GetWorld() && IsLivingActor(Controller->GetPawn())
		? Controller->GetSkillLockedTarget() : nullptr;
	RefreshFromTarget(Target == GetOwningPlayerPawn() ? nullptr : Target);
}

void URPGCombatContributionWidget::RefreshFromTarget(AActor* Target)
{
	if (!bAttached || !GetWorld() || GetWorld()->bIsTearingDown || !IsLivingActor(Target)
		|| Target->GetWorld() != GetWorld()) { ClearPresentation(); return; }
	const auto* Receiver = Target->FindComponentByClass<URPGCombatContributionComponent>();
	if (!IsValid(Receiver)) { ClearPresentation(); return; }
	const auto State = Receiver->GetContributionState();
	if (!State.bEnabled || (State.StaggerThreshold <= 0.f && State.PartBreakThreshold <= 0.f)
		|| !IsCoherentMeter(State.Stagger, State.StaggerThreshold, State.bStaggerComplete)
		|| !IsCoherentMeter(State.PartBreak, State.PartBreakThreshold, State.bPartBroken))
	{
		ClearPresentation(); return;
	}
	DisplayedTarget = Target;
	ShowState(State);
}

void URPGCombatContributionWidget::ShowState(const FRPGCombatContributionState& State)
{
	if (!Card || MeterRows.Num() != 2) return;
	const float Values[] = { State.Stagger, State.PartBreak };
	const float Thresholds[] = { State.StaggerThreshold, State.PartBreakThreshold };
	const bool Completed[] = { State.bStaggerComplete, State.bPartBroken };
	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const bool bSupported = Thresholds[Index] > 0.f;
		MeterRows[Index]->SetVisibility(bSupported ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Meters[Index]->SetPercent(bSupported ? Values[Index] / Thresholds[Index] : 0.f);
		Meters[Index]->SetFillColorAndOpacity(Completed[Index] ? FLinearColor(.3f, .9f, .5f)
			: Index ? FLinearColor(.35f, .7f, 1.f) : FLinearColor(1.f, .75f, .2f));
		FText Text;
		if (bSupported)
		{
			const FText Name = Index ? LOCTEXT("PartBreak", "부위 파괴 기여") : LOCTEXT("Stagger", "무력화 기여");
			Text = FText::Format(Completed[Index] ? LOCTEXT("Complete", "{0} 완료 ({1}/{2})") : LOCTEXT("Progress", "{0} {1}/{2}"),
				Name, FText::AsNumber(Values[Index], &Format), FText::AsNumber(Thresholds[Index], &Format));
		}
		if (!Labels[Index]->GetText().EqualTo(Text)) Labels[Index]->SetText(Text);
	}
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void URPGCombatContributionWidget::ClearPresentation()
{
	DisplayedTarget.Reset();
	if (Card) Card->SetVisibility(ESlateVisibility::Collapsed);
	for (const auto& Label : Labels) if (Label) Label->SetText(FText::GetEmpty());
	for (const auto& Meter : Meters) if (Meter) Meter->SetPercent(0.f);
	for (const auto& Row : MeterRows) if (Row) Row->SetVisibility(ESlateVisibility::Collapsed);
}

void URPGCombatContributionWidget::NativeDestruct()
{
	bAttached = false;
	ClearPresentation();
	Super::NativeDestruct();
}

#undef LOCTEXT_NAMESPACE
