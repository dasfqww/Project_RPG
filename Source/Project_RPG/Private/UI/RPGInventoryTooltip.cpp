// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGInventoryTooltip.h"
#include "Item/RPGItemBase.h"
#include "UI/RPGInventoryItemSlot.h"
#include "Components/TextBlock.h"
#include "UI/HUD/RPGHUD.h"
#include "Components/SizeBox.h"

URPGInventoryTooltip::URPGInventoryTooltip()
{
}

void URPGInventoryTooltip::NativeConstruct()
{
	Super::NativeConstruct();

	

	/*const URPGItemBase* ItemBeingHovered = InventorySlotBeingHovered->GetItemReference();

	if (ItemBeingHovered)
	{
		switch (ItemBeingHovered->ItemGrade)
		{
		case EItemGrade::Common:
			ItemType->SetColorAndOpacity(FLinearColor::Gray);
			break;
		case EItemGrade::Advanced:
			ItemType->SetColorAndOpacity(FLinearColor::Green);
			break;
		case EItemGrade::Rare:
			ItemType->SetColorAndOpacity(FLinearColor::Blue);
			break;
		case EItemGrade::Hero:
			ItemType->SetColorAndOpacity(FLinearColor::Red);
			break;
		case EItemGrade::Legend:
			ItemType->SetColorAndOpacity(FLinearColor::Yellow);
			break;
		default:;
		}

		switch (ItemBeingHovered->ItemType)
		{
		case EItemType::Armor:
			break;

		case EItemType::Weapon:
			break;

		case EItemType::Comsumable:
			ItemType->SetText(FText::FromString("Consumable"));
			DamageValue->SetVisibility(ESlateVisibility::Collapsed);
			ArmorRating->SetVisibility(ESlateVisibility::Collapsed);
			//SellValue->SetVisibility(ESlateVisibility::Collapsed);
			break;

		case EItemType::Quest:
			break;

		case EItemType::Mundane:
			ItemType->SetText(FText::FromString("Mundane"));
			DamageValue->SetVisibility(ESlateVisibility::Collapsed);
			ArmorRating->SetVisibility(ESlateVisibility::Collapsed);
			UsageText->SetVisibility(ESlateVisibility::Collapsed);
			//SellValue->SetVisibility(ESlateVisibility::Collapsed);
			break;

		default:;
		}

 		ItemName->SetText(ItemBeingHovered->TextData.Name);
		DamageValue->SetText(FText::AsNumber(ItemBeingHovered->Statistics.Attack));
		ArmorRating->SetText(FText::AsNumber(ItemBeingHovered->Statistics.Defense));
		UsageText->SetText(ItemBeingHovered->TextData.UsageText);
		ItemDescription->SetText(ItemBeingHovered->TextData.Description);
		//SellValue->SetText(FText::AsNumber(ItemBeingHovered->Statistics.SellValue));
		//StackWeight->SetText(FText::AsNumber(ItemBeingHovered->GetItemStackWeight()));

		const FString WeightInfo =
		{ "Weight: " + FString::SanitizeFloat(ItemBeingHovered->GetItemStackWeight()) };

		StackWeight->SetText(FText::FromString(WeightInfo));

		if (ItemBeingHovered->NumericData.bIsStackable)
		{
			const FString StackInfo =
			{ "Max stack size: " + FString::FromInt(ItemBeingHovered->NumericData.MaxStackSize) };

			MaxStackSize->SetText(FText::FromString(StackInfo));
		}
		else
		{
			MaxStackSize->SetVisibility(ESlateVisibility::Collapsed);
		}
	}*/
}

FVector2D URPGInventoryTooltip::GetBoxSize() const
{
	return SizeBox->GetDesiredSize();
}
