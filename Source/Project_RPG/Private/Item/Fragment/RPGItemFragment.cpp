// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/Fragment/RPGItemFragment.h"
#include "UI/Composite/RPGCompositeBase.h"	
#include "UI/Composite/RPGLeafImage.h"
#include "UI/Composite/RPGLeafText.h"
#include "UI/Composite/RPGLeafLabledValue.h"

#include "RPGDebugHelper.h"

//void FConsumableFragment::OnConsume(APlayerController* PC)
//{
//	// Get a stats component from the PC or the PC->GetPawn()
//	// or get the Ability System Component and apply a Gameplay Effect
//	// or call an interface function for Healing()
//
//	//
//}

void FInventoryItemFragment::Assimilate(URPGCompositeBase* Composite) const
{
	if (!MatchesWidgetTag(Composite)) return;
	Composite->Expand();
}

bool FInventoryItemFragment::MatchesWidgetTag(const URPGCompositeBase* Composite) const
{
	return Composite->GetFragmentTag().MatchesTagExact(GetFragmentTag());
}

void FImageFragment::Assimilate(URPGCompositeBase* Composite) const
{
	FInventoryItemFragment::Assimilate(Composite);

	/*if (Composite)
	{
		UE_LOG(LogTemp, Log, TEXT("Composite image GameplayTag: %s"),
			*Composite->GetFragmentTag().ToString());
	}*/

	if (!MatchesWidgetTag(Composite)) return;

	URPGLeafImage* Image = Cast<URPGLeafImage>(Composite);
	if (!IsValid(Image)) return;

	Image->SetImage(Icon);
	Image->SetBoxSize(IconDimensions);
	Image->SetImageSize(IconDimensions);
}

void FTextFragment::Assimilate(URPGCompositeBase* Composite) const
{
	FInventoryItemFragment::Assimilate(Composite);
	/*if (Composite)
	{
		UE_LOG(LogTemp, Log, TEXT("Composite text GameplayTag: %s"),
			*Composite->GetFragmentTag().ToString());
	}*/
	if (!MatchesWidgetTag(Composite)) return;

	URPGLeafText* LeafText = Cast<URPGLeafText>(Composite);
	if (!IsValid(LeafText)) return;

	LeafText->SetText(FragmentText);
}

void FLabeledNumberFragment::Assimilate(URPGCompositeBase* Composite) const
{
	FInventoryItemFragment::Assimilate(Composite);

	if (!MatchesWidgetTag(Composite)) return;

	URPGLeafLabledValue* LabeledValue = Cast<URPGLeafLabledValue>(Composite);
	if (!IsValid(LabeledValue)) return;

	LabeledValue->SetLabelText(Text_Label, bCollapseLabel);

	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = MinFractionalDigits;
	Options.MaximumFractionalDigits = MaxFractionalDigits;

	LabeledValue->SetValueText(FText::AsNumber(Value, &Options), bCollapseValue);
}

void FLabeledNumberFragment::Manifest()
{
	FInventoryItemFragment::Manifest();

	if (bRandomizeOnManifest)
	{
		Value = FMath::FRandRange(Min, Max);
	}
	bRandomizeOnManifest = false;
}

void FHealthPotionFragment::OnConsume(APlayerController* PC)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, 
		FString::Printf(TEXT("Health Potion consumed! Healing by: %f"), GetValue()));
}

void FConsumableFragment::OnConsume(APlayerController* PC)
{
	for (auto& Modifier : ConsumeModifiers)
	{
		auto& ModRef = Modifier.GetMutable();
		ModRef.OnConsume(PC);
	}
}

void FConsumableFragment::Assimilate(URPGCompositeBase* Composite) const
{
	FInventoryItemFragment::Assimilate(Composite);
	for (const auto& Modifier : ConsumeModifiers)
	{
		const auto& ModRef = Modifier.Get();
		ModRef.Assimilate(Composite);
	}
}

void FConsumableFragment::Manifest()
{
	FInventoryItemFragment::Manifest();
	for (auto& Modifier : ConsumeModifiers)
	{
		auto& ModRef = Modifier.GetMutable();
		ModRef.Manifest();
	}
}

void FStatModifier::OnEquip(APlayerController* PC)
{
	/*GEngine->AddOnScreenDebugMessage(
		-1,
		5.f,
		FColor::Green,
		FString::Printf(TEXT("Strength increased by: %f"),
			GetValue()));*/
}

void FStatModifier::OnUnequip(APlayerController* PC)
{
}

void FEquipmentFragment::OnEquip(APlayerController* PC)
{
	if (bEquipped) return;
	bEquipped = true;
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModRef = Modifier.GetMutable();
		ModRef.OnEquip(PC);
	}
}

void FEquipmentFragment::OnUnequip(APlayerController* PC)
{
	if (!bEquipped) return;
	bEquipped = false;
	for (auto& Modifier : EquipModifiers)
	{
		auto& ModRef = Modifier.GetMutable();
		ModRef.OnUnequip(PC);
	}
}

void FEquipmentFragment::Assimilate(URPGCompositeBase* Composite) const
{
	FInventoryItemFragment::Assimilate(Composite);
	for (const auto& Modifier : EquipModifiers)
	{
		const auto& ModRef = Modifier.Get();
		ModRef.Assimilate(Composite);
	}
}
