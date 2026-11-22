#include "UI/Vitals/RPGPlayerVitalsWidget.h"
#include "UI/Vitals/RPGPlayerShieldWidget.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
void ReadResource(const float Current, const float Maximum, float& Value, float& Max, float& Fraction)
{
	Max = FMath::IsFinite(Maximum) ? FMath::Max(0.f, Maximum) : 0.f;
	Value = FMath::IsFinite(Current) ? FMath::Clamp(Current, 0.f, Max) : 0.f;
	Fraction = Max > 0.f ? Value / Max : 0.f;
}
FText ResourceText(const float Current, const float Maximum)
{
	FNumberFormattingOptions Format;
	Format.SetUseGrouping(false).SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(0);
	return FText::Format(NSLOCTEXT("RPGPlayerVitals", "CurrentMaximum", "{0}/{1}"),
		FText::AsNumber(Current, &Format), FText::AsNumber(Maximum, &Format));
}
}

void URPGPlayerVitalsWidget::SetVitalsSource(UAbilitySystemComponent* Source)
{
	UnbindSource();
	VitalsSource = Source;
	BindAndRefresh();
}

void URPGPlayerVitalsWidget::UnbindSource()
{
	if (auto* ASC = BoundSource.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentManaAttribute()).Remove(ManaHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxManaAttribute()).Remove(MaxManaHandle);
	}
	if (auto* Actor = SourceActor.Get()) Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::OnSourceEndPlay);
	BoundSource.Reset(); SourceActor.Reset();
	HealthHandle.Reset(); MaxHealthHandle.Reset(); ManaHandle.Reset(); MaxManaHandle.Reset();
}

void URPGPlayerVitalsWidget::BindAndRefresh()
{
	auto* ASC = VitalsSource.Get();
	if (bConstructed && ASC && ASC->IsRegistered())
	{
		BoundSource = ASC;
		HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddUObject(this, &ThisClass::OnAttributeChanged);
		MaxHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::OnAttributeChanged);
		ManaHandle = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentManaAttribute()).AddUObject(this, &ThisClass::OnAttributeChanged);
		MaxManaHandle = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxManaAttribute()).AddUObject(this, &ThisClass::OnAttributeChanged);
		SourceActor = ASC->GetAvatarActor() ? ASC->GetAvatarActor() : ASC->GetOwner();
		if (auto* Actor = SourceActor.Get()) Actor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnSourceEndPlay);
	}
	RefreshPresentation();
}

void URPGPlayerVitalsWidget::UnbindController()
{
	if (auto* PC = BoundController.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnOwningPawnChanged);
	BoundController.Reset();
}

void URPGPlayerVitalsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UnbindController(); UnbindSource();
	bConstructed = true;
	if (auto* PC = GetOwningPlayer(); bAutoBindOwningPawn && PC && PC->IsLocalController())
	{
		BoundController = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnOwningPawnChanged);
		OnOwningPawnChanged(nullptr, PC->GetPawn());
	}
	else BindAndRefresh();
}

void URPGPlayerVitalsWidget::NativeDestruct()
{
	bConstructed = false;
	UnbindController(); UnbindSource(); RefreshPresentation();
	Super::NativeDestruct();
}

void URPGPlayerVitalsWidget::BeginDestroy()
{
	bConstructed = false;
	UnbindController(); UnbindSource();
	Super::BeginDestroy();
}

void URPGPlayerVitalsWidget::OnOwningPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (BoundController.Get() != GetOwningPlayer()) return;
	SetVitalsSource(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(NewPawn, true));
}

void URPGPlayerVitalsWidget::OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	if (Actor == SourceActor.Get()) SetVitalsSource(nullptr);
}

void URPGPlayerVitalsWidget::OnAttributeChanged(const FOnAttributeChangeData& Change)
{
	RefreshPresentation();
}

void URPGPlayerVitalsWidget::RefreshPresentation()
{
	FRPGPlayerVitalsSnapshot Next;
	const auto* ASC = VitalsSource.Get();
	if (bConstructed && ASC && ASC->IsRegistered() && IsValid(ASC->GetOwner())
		&& !ASC->GetOwner()->IsActorBeingDestroyed()
		&& ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute())
		&& ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute())
		&& ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentManaAttribute())
		&& ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxManaAttribute()))
	{
		Next.bHasSource = true;
		ReadResource(ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()),
			ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()), Next.Health, Next.MaxHealth, Next.HealthFraction);
		ReadResource(ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentManaAttribute()),
			ASC->GetNumericAttribute(URPGAttributeSet::GetMaxManaAttribute()), Next.Mana, Next.MaxMana, Next.ManaFraction);
	}
	DisplayedVitals = Next;
	ApplyPresentation();
}

void URPGPlayerVitalsWidget::ApplyPresentation()
{
	const auto& State = DisplayedVitals;
	SetVisibility(State.bHasSource ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (HealthBar) HealthBar->SetPercent(State.HealthFraction);
	if (ManaBar) ManaBar->SetPercent(State.ManaFraction);
	RefreshHealthText();
	if (ManaText) ManaText->SetText(State.bHasSource ? ResourceText(State.Mana, State.MaxMana) : FText::GetEmpty());
	// Cosmetic callbacks may detach or replace the source; no state writes follow.
	if (bConstructed) OnVitalsPresentationChanged();
}

void URPGPlayerVitalsWidget::RefreshHealthText()
{
	if (!HealthText) return;
	const auto& State = DisplayedVitals;
	FText Text = State.bHasSource ? ResourceText(State.Health, State.MaxHealth) : FText::GetEmpty();
	const auto* Shield = ShieldView.Get();
	const float Remaining = State.bHasSource && Shield ? Shield->GetDisplayedAmountFor(VitalsSource.Get()) : 0.f;
	if (Remaining > 0.f)
	{
		FNumberFormattingOptions Format;
		Format.SetUseGrouping(false).SetMaximumFractionalDigits(1);
		const FText Amount = Remaining < .1f
			? FText::Format(NSLOCTEXT("RPGPlayerVitals", "BelowShieldDisplayPrecision", "<{0}"), FText::AsNumber(.1f, &Format))
			: FText::AsNumber(Remaining, &Format);
		Text = FText::Format(NSLOCTEXT("RPGPlayerVitals", "HealthWithShield", "{0} (+{1})"), Text, Amount);
	}
	HealthText->SetText(Text);
}

TSharedRef<SWidget> URPGPlayerVitalsWidget::RebuildWidget()
{
	SetIsFocusable(false);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Root = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Root;
		HealthText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthText"));
		HealthBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
		HealthBar->SetFillColorAndOpacity(FLinearColor(.7f, .07f, .1f));
		ManaText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ManaText"));
		ManaBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ManaBar"));
		ManaBar->SetFillColorAndOpacity(FLinearColor(.1f, .25f, .9f));
		Root->AddChildToVerticalBox(HealthText); Root->AddChildToVerticalBox(HealthBar);
		Root->AddChildToVerticalBox(ManaText); Root->AddChildToVerticalBox(ManaBar);
	}
	if (!IsDesignTime() && WidgetTree && HealthBar)
	{
		auto* Shield = Cast<URPGPlayerShieldWidget>(WidgetTree->FindWidget(TEXT("PlayerShield")));
		auto* BarContainer = Cast<UContentWidget>(HealthBar->GetParent());
		if (auto* Previous = ShieldView.Get()) Previous->OnDisplayedShieldChanged.RemoveAll(this);
		ShieldView = Shield;
		if (Shield) Shield->OnDisplayedShieldChanged.AddUObject(this, &ThisClass::RefreshHealthText);
		if (Shield && BarContainer)
		{
			// Fit both fills inside the existing border padding. The authored labels
			// remain siblings above the border, so they stay legible over both fills.
			auto* Fills = WidgetTree->ConstructWidget<UOverlay>();
			BarContainer->SetContent(Fills);
			Shield->RemoveFromParent();
			for (auto* Fill : {static_cast<UWidget*>(HealthBar.Get()), static_cast<UWidget*>(Shield)})
			{
				auto* FillSlot = Fills->AddChildToOverlay(Fill);
				FillSlot->SetHorizontalAlignment(HAlign_Fill);
				FillSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}
	}
	return Super::RebuildWidget();
}
