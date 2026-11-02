#include "UI/Vitals/RPGPlayerShieldWidget.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Combat/RPGShieldComponent.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void URPGPlayerShieldWidget::SetShieldSource(URPGShieldComponent* Next)
{
	UnbindSource();
	const auto* PC = GetOwningPlayer();
	Source = Next && (!PC || (PC->IsLocalController() && Next->GetOwner() == PC->GetPawn())) ? Next : nullptr;
	BindSource();
	RefreshPresentation();
}

void URPGPlayerShieldWidget::BindSource()
{
	auto* Shield = Source.Get();
	auto* Actor = Shield ? Shield->GetOwner() : nullptr;
	if (!bConstructed || !Shield || !Shield->IsRegistered() || !IsValid(Actor) || Actor->IsActorBeingDestroyed()) return;
	BoundActor = Actor;
	Shield->OnShieldChanged.AddUniqueDynamic(this, &ThisClass::OnShieldChanged);
	Actor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnSourceEndPlay);
	BoundASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
	if (auto* ASC = BoundASC.Get())
		HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddUObject(this, &ThisClass::OnHealthChanged);
}

void URPGPlayerShieldWidget::UnbindSource()
{
	if (auto* Shield = Source.Get()) Shield->OnShieldChanged.RemoveDynamic(this, &ThisClass::OnShieldChanged);
	if (auto* Actor = BoundActor.Get()) Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::OnSourceEndPlay);
	if (auto* ASC = BoundASC.Get()) ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthHandle);
	BoundActor.Reset(); BoundASC.Reset(); HealthHandle.Reset(); RefreshElapsed = 0.f;
}

void URPGPlayerShieldWidget::UnbindController()
{
	if (auto* PC = Controller.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnOwningPawnChanged);
	Controller.Reset();
}

void URPGPlayerShieldWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UnbindController(); UnbindSource(); bConstructed = true;
	if (auto* PC = GetOwningPlayer(); PC && PC->IsLocalController())
	{
		Controller = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnOwningPawnChanged);
		OnOwningPawnChanged(nullptr, PC->GetPawn());
	}
	else { BindSource(); RefreshPresentation(); }
}

void URPGPlayerShieldWidget::NativeDestruct()
{
	bConstructed = false;
	UnbindController(); UnbindSource(); Source.Reset(); RefreshPresentation();
	Super::NativeDestruct();
}

void URPGPlayerShieldWidget::BeginDestroy()
{
	bConstructed = false;
	UnbindController(); UnbindSource(); Source.Reset();
	Super::BeginDestroy();
}

void URPGPlayerShieldWidget::OnOwningPawnChanged(APawn*, APawn* Next)
{
	if (Controller.Get() != GetOwningPlayer()) return;
	SetShieldSource(Next ? Next->FindComponentByClass<URPGShieldComponent>() : nullptr);
}

void URPGPlayerShieldWidget::OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type)
{
	if (Actor == BoundActor.Get()) SetShieldSource(nullptr);
}

void URPGPlayerShieldWidget::OnShieldChanged(const FRPGShieldPresentation&) { RefreshPresentation(); }
void URPGPlayerShieldWidget::OnHealthChanged(const FOnAttributeChangeData&) { RefreshPresentation(); }

void URPGPlayerShieldWidget::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	RefreshElapsed += DeltaSeconds;
	if (Displayed.bVisible && RefreshElapsed >= .1f) { RefreshElapsed = 0.f; RefreshPresentation(); }
}

void URPGPlayerShieldWidget::RefreshPresentation()
{
	FRPGShieldHUDSnapshot Next;
	const auto* Shield = Source.Get();
	const auto* Actor = BoundActor.Get();
	const auto* ASC = BoundASC.Get();
	const auto* Stats = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
	const auto* PC = GetOwningPlayer();
	if (bConstructed && Shield && Shield->IsRegistered() && IsValid(Actor) && !Actor->IsActorBeingDestroyed()
		&& ASC && ASC->GetAvatarActor() == Actor && Stats && FMath::IsFinite(Stats->GetCurrentHealth()) && Stats->GetCurrentHealth() > 0.f
		&& (!PC || (PC->IsLocalController() && PC->GetPawn() == Actor)))
	{
		const auto State = Shield->GetShieldState();
		const auto* World = Actor->GetWorld();
		const auto* GameState = World ? World->GetGameState() : nullptr;
		const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.;
		if (FMath::IsFinite(State.Remaining) && State.Remaining > 0.f && FMath::IsFinite(State.ExpiresAt) && State.ExpiresAt > Now)
		{
			Next.bVisible = true; Next.Remaining = State.Remaining;
			Next.SecondsRemaining = static_cast<float>(State.ExpiresAt - Now);
		}
	}
	Displayed = Next;
	SetVisibility(Next.bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	FNumberFormattingOptions AmountFormat; AmountFormat.SetUseGrouping(false).SetMaximumFractionalDigits(1);
	FNumberFormattingOptions TimeFormat; TimeFormat.SetUseGrouping(false).SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
	if (ShieldText) ShieldText->SetText(Next.bVisible
		? FText::Format(NSLOCTEXT("RPGShieldHUD", "Remaining", "보호막 +{0}"), FText::AsNumber(Next.Remaining, &AmountFormat)) : FText::GetEmpty());
	if (ShieldTimeText) ShieldTimeText->SetText(Next.bVisible
		? FText::Format(NSLOCTEXT("RPGShieldHUD", "Time", "{0}초"), FText::AsNumber(Next.SecondsRemaining, &TimeFormat)) : FText::GetEmpty());
}

TSharedRef<SWidget> URPGPlayerShieldWidget::RebuildWidget()
{
	SetIsFocusable(false);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Root = WidgetTree->ConstructWidget<UHorizontalBox>(); WidgetTree->RootWidget = Root;
		ShieldText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShieldText"));
		ShieldTimeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ShieldTimeText"));
		for (auto* Label : {ShieldText.Get(), ShieldTimeText.Get()})
		{
			auto Font = Label->GetFont(); Font.Size = 18; Label->SetFont(Font);
			Label->SetColorAndOpacity(FSlateColor(FLinearColor(.15f, .85f, 1.f)));
			Label->SetShadowOffset(FVector2D(1.f, 1.f)); Label->SetShadowColorAndOpacity(FLinearColor::Black);
			Root->AddChildToHorizontalBox(Label)->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
		}
	}
	return Super::RebuildWidget();
}
