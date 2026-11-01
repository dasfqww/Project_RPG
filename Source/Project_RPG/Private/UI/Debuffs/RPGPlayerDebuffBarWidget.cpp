#include "UI/Debuffs/RPGPlayerDebuffBarWidget.h"

#include "UI/Debuffs/RPGDebuffIconWidget.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void URPGPlayerDebuffBarWidget::UnbindController()
{
	if (auto* PC = BoundController.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnOwningPawnChanged);
	BoundController.Reset();
}
void URPGPlayerDebuffBarWidget::SetDebuffSource(UAbilitySystemComponent* Value)
{
	bAutoBindOwningPawn = false; UnbindController(); Source = Value;
	Displayed.Reset(); RefreshPresentation();
}
void URPGPlayerDebuffBarWidget::FollowOwner()
{
	auto* PC = GetOwningPlayer();
	if (!PC || !PC->IsLocalController()) { UnbindController(); Source.Reset(); return; }
	if (BoundController.Get() != PC)
	{
		UnbindController(); BoundController = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnOwningPawnChanged);
	}
	APawn* Pawn = PC->GetPawn();
	auto* Next = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true);
	if (Next && Next->GetAvatarActor() != Pawn) Next = nullptr;
	if (Source.Get() != Next) { Source = Next; Displayed.Reset(); }
}
void URPGPlayerDebuffBarWidget::OnOwningPawnChanged(APawn*, APawn*) { FollowOwner(); RefreshPresentation(); }
void URPGPlayerDebuffBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (bAutoBindOwningPawn) FollowOwner();
	RefreshPresentation();
}
void URPGPlayerDebuffBarWidget::NativeDestruct()
{
	UnbindController(); Source.Reset(); Displayed.Reset(); ApplyPresentation();
	Super::NativeDestruct();
}
void URPGPlayerDebuffBarWidget::BeginDestroy() { UnbindController(); Source.Reset(); Super::BeginDestroy(); }
void URPGPlayerDebuffBarWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	RefreshElapsed += DeltaSeconds;
	if (RefreshElapsed < .1f) return;
	RefreshElapsed = 0.f;
	if (bAutoBindOwningPawn) FollowOwner();
	RefreshPresentation();
}
void URPGPlayerDebuffBarWidget::RefreshPresentation()
{
	const auto* World = GetWorld();
	Displayed = RPGDebuffPresentation::StableOrder(RPGDebuffPresentation::Read(Source.Get(), World ? World->GetTimeSeconds() : 0.), Displayed);
	ApplyPresentation();
}
TSharedRef<SWidget> URPGPlayerDebuffBarWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		DebuffList = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DebuffList"));
		WidgetTree->RootWidget = DebuffList;
		CellPool.SetNum(8); TimerPool.SetNum(8);
	}
	Entries.Reset(); ApplyPresentation();
	return Super::RebuildWidget();
}
void URPGPlayerDebuffBarWidget::ApplyPresentation()
{
	if (!DebuffList || !WidgetTree) return;
	bool bChanged = Entries.Num() != Displayed.Num();
	if (!bChanged) for (int32 I = 0; I < Entries.Num(); ++I) bChanged |= Entries[I].Kind != Displayed[I].Kind;
	if (bChanged)
	{
		DebuffList->ClearChildren(); Entries.Reset();
		for (const auto& Item : Displayed)
		{
			const int32 Index = int32(Item.Kind);
			if (CellPool.IsValidIndex(Index) && CellPool[Index])
			{
				auto* CellSlot = DebuffList->AddChildToHorizontalBox(CellPool[Index]);
				CellSlot->SetPadding(FMargin(0,0,6,0)); CellSlot->SetVerticalAlignment(VAlign_Top);
				Entries.Add({Item.Kind, TimerPool[Index]}); continue;
			}
			const FString Suffix = FString::FromInt(int32(Item.Kind));
			auto* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(TEXT("DebuffCell_") + Suffix));
			Box->SetWidthOverride(42); Box->SetHeightOverride(58);
			Box->SetToolTipText(RPGDebuffPresentation::Name(Item.Kind));
			auto* Column = WidgetTree->ConstructWidget<UVerticalBox>(); Box->AddChild(Column);
			auto* Icon = WidgetTree->ConstructWidget<URPGDebuffIconWidget>(URPGDebuffIconWidget::StaticClass(), FName(TEXT("DebuffIcon_") + Suffix));
			Icon->SetKind(Item.Kind);
			auto* IconSize = WidgetTree->ConstructWidget<USizeBox>(); IconSize->SetWidthOverride(36); IconSize->SetHeightOverride(36); IconSize->AddChild(Icon);
			auto* IconSlot = Column->AddChildToVerticalBox(IconSize); IconSlot->SetHorizontalAlignment(HAlign_Center);
			auto* Time = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(TEXT("DebuffTime_") + Suffix));
			auto Font = Time->GetFont(); Font.Size = 12; Time->SetFont(Font);
			Time->SetJustification(ETextJustify::Center); Time->SetColorAndOpacity(FSlateColor(FLinearColor(.95f,.96f,1.f)));
			Time->SetShadowColorAndOpacity(FLinearColor::Black); Time->SetShadowOffset(FVector2D(1,1));
			auto* TextSlot = Column->AddChildToVerticalBox(Time); TextSlot->SetHorizontalAlignment(HAlign_Center); TextSlot->SetPadding(FMargin(0,2,0,0));
			auto* CellSlot = DebuffList->AddChildToHorizontalBox(Box); CellSlot->SetPadding(FMargin(0,0,6,0)); CellSlot->SetVerticalAlignment(VAlign_Top);
			CellPool[Index] = Box; TimerPool[Index] = Time;
			Entries.Add({Item.Kind, Time});
		}
	}
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		if (auto* Time = Entries[I].Time.Get())
		{
			const FText Text = RPGDebuffPresentation::TimeText(Displayed[I]);
			if (!Time->GetText().EqualTo(Text)) Time->SetText(Text);
			Time->SetColorAndOpacity(FSlateColor(Displayed[I].bTimed && Displayed[I].RemainingSeconds < 3. ? FLinearColor(1.f,.55f,.4f) : FLinearColor(.95f,.96f,1.f)));
		}
	}
}
