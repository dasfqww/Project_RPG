// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/RPGRewardItemWidget.h"
#include "Item/RPGItemBase.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

URPGRewardItemWidget::URPGRewardItemWidget()
{
}

void URPGRewardItemWidget::NativeConstruct()
{
    Super::NativeConstruct();

    /*if (ItemReference)
    {
        switch (ItemReference->ItemGrade)
        {
        case EItemGrade::Common:
            ItemBorder->SetBrushColor(FLinearColor::Gray);
            break;
        case EItemGrade::Advanced:
            ItemBorder->SetBrushColor(FLinearColor::Green);
            break;
        case EItemGrade::Rare:
            ItemBorder->SetBrushColor(FLinearColor::Blue);
            break;
        case EItemGrade::Hero:
            ItemBorder->SetBrushColor(FLinearColor::Red);
            break;
        case EItemGrade::Legend:
            ItemBorder->SetBrushColor(FLinearColor::Yellow);
            break;
        }

        ItemIcon->SetBrushFromTexture(ItemReference->AssetData.Icon);

        if (ItemReference->NumericData.bIsStackable)
        {
            ItemQuantity->SetText(FText::AsNumber(ItemReference->Quantity));
        }
        else
        {
            ItemQuantity->SetVisibility(ESlateVisibility::Collapsed);
        }
    }*/

}

void URPGRewardItemWidget::InitializeRewardItems()
{

}
