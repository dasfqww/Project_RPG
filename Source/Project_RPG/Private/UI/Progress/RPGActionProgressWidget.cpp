#include "UI/Progress/RPGActionProgressWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#define LOCTEXT_NAMESPACE "RPGActionProgressWidget"

void URPGActionProgressWidget::SetProgressSource(UPlayerUIComponent* Source)
{
	Unsubscribe();
	ProgressSource = Source;
	BindAndRefresh();
}

void URPGActionProgressWidget::Unsubscribe()
{
	if (UPlayerUIComponent* Source = ProgressSource.Get())
		Source->OnActionProgressChangedNative.Remove(ChangeHandle);
	ChangeHandle.Reset();
}

void URPGActionProgressWidget::BindAndRefresh()
{
	if (bConstructed)
	{
		if (UPlayerUIComponent* Source = ProgressSource.Get())
			ChangeHandle = Source->OnActionProgressChangedNative.AddUObject(this, &ThisClass::RefreshPresentation);
	}
	RefreshPresentation();
}

TSharedRef<SWidget> URPGActionProgressWidget::RebuildWidget()
{
	SetIsFocusable(false);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Content;
		ActionIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ActionIcon"));
		ActionIcon->SetDesiredSizeOverride(FVector2D(32.f, 32.f));
		Content->AddChildToVerticalBox(ActionIcon);
		ActionLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ActionLabel"));
		Content->AddChildToVerticalBox(ActionLabel);
		ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ProgressBar"));
		Content->AddChildToVerticalBox(ProgressBar);
		TimeLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TimeLabel"));
		Content->AddChildToVerticalBox(TimeLabel);
		StageLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StageLabel"));
		Content->AddChildToVerticalBox(StageLabel);
		PhaseLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PhaseLabel"));
		Content->AddChildToVerticalBox(PhaseLabel);
	}
	return Super::RebuildWidget();
}

void URPGActionProgressWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Unsubscribe();
	bConstructed = true;
	BindAndRefresh();
}

void URPGActionProgressWidget::NativeDestruct()
{
	bConstructed = false;
	Unsubscribe();
	RefreshPresentation();
	Super::NativeDestruct();
}

void URPGActionProgressWidget::BeginDestroy()
{
	bConstructed = false;
	Unsubscribe();
	Super::BeginDestroy();
}

void URPGActionProgressWidget::RefreshPresentation()
{
	const UPlayerUIComponent* Source = ProgressSource.Get();
	DisplayedProgress = bConstructed && Source ? Source->GetActionProgressSnapshot() : FRPGActionProgressSnapshot{};
	ApplyPresentation();
}

void URPGActionProgressWidget::ApplyPresentation()
{
	const auto& State = DisplayedProgress;
	const auto& Data = State.Data;
	SetVisibility(State.bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (ActionLabel) ActionLabel->SetText(State.bActive ? Data.Label : FText::GetEmpty());
	if (ProgressBar)
	{
		ProgressBar->SetIsMarquee(State.bActive && !Data.bHasDuration);
		ProgressBar->SetPercent(State.bActive ? State.Fraction : 0.f);
	}
	if (TimeLabel)
	{
		const bool bShow = State.bActive && Data.TimeDisplay != ERPGActionProgressTimeDisplay::None;
		FNumberFormattingOptions Format;
		Format.MinimumFractionalDigits = Format.MaximumFractionalDigits = 1;
		TimeLabel->SetText(bShow ? FText::Format(LOCTEXT("Seconds", "{0}s"),
			FText::AsNumber(State.DisplaySeconds, &Format)) : FText::GetEmpty());
		TimeLabel->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (StageLabel)
	{
		const bool bShow = State.bActive && Data.StageCount > 0;
		StageLabel->SetText(bShow ? FText::Format(LOCTEXT("Stage", "{0} / {1}"),
			FText::AsNumber(Data.Stage), FText::AsNumber(Data.StageCount)) : FText::GetEmpty());
		StageLabel->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (PhaseLabel)
	{
		FText Text;
		if (State.bActive && Data.Phase == ERPGActionProgressPhase::Ready) Text = LOCTEXT("Ready", "Ready");
		if (State.bActive && Data.Phase == ERPGActionProgressPhase::AwaitingConfirmation) Text = LOCTEXT("Waiting", "Awaiting confirmation");
		PhaseLabel->SetText(Text);
		PhaseLabel->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (ActionIcon)
	{
		const auto Icon = State.bActive ? Data.Icon : TSoftObjectPtr<UTexture2D>{};
		if (DisplayedIcon != Icon || Icon.IsNull())
		{
			DisplayedIcon = Icon;
			// UImage cancels old streaming, including callbacks queued for a previous path.
			ActionIcon->SetBrushFromTexture(nullptr);
			if (!Icon.IsNull()) ActionIcon->SetBrushFromSoftTexture(Icon);
		}
		ActionIcon->SetVisibility(Icon.IsNull() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	// No state writes after BP: it may bind another source or detach this widget.
	if (bConstructed) OnProgressPresentationChanged();
}

#undef LOCTEXT_NAMESPACE
