// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Content/RPGContentEntryWidget.h"
#include "Components/WidgetSwitcher.h"
#include "DataTable/DropItemData.h"
#include "Item/RPGItemBase.h"
#include "UI/RPGRewardItemWidget.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "GameMode/RPGGameModeBase.h"
#include "UI/MessageBox/RPGMessageBox.h"
#include "GameInstance/RPGGameInstance.h"
#include "RPGFunctionLibrary.h"
#include "Manager/UIManager.h"

URPGContentEntryWidget::URPGContentEntryWidget(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer),
	GameDifficulty(ERPGGameDifficulty::Easy)
{

}

void URPGContentEntryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	URPGFunctionLibrary::ToggleInputMode(GetWorld(), ERPGInputMode::UIOnly);

	//UpdateRewardUIByDifficulty(GameDifficulty);
}

void URPGContentEntryWidget::OnDifficultyButtonClicked(ERPGGameDifficulty InGameDifficulty)
{
	//UpdateRewardUIByDifficulty(InGameDifficulty);
}

//void URPGContentEntryWidget::UpdateRewardUIByDifficulty(ERPGGameDifficulty InGameDifficulty)
//{
//	RewardInfoPanel->ClearChildren();
//
//	FName RowName;
//
//	switch (InGameDifficulty)
//	{
//	case ERPGGameDifficulty::Easy:
//		RowName = "EasyReward";
//		GameDifficulty = ERPGGameDifficulty::Easy;
//		ContentNameText->SetText(FText::FromString(TEXT("SurvivalMode(Easy)")));
//		break;
//	case ERPGGameDifficulty::Normal:
//		RowName = "NormalReward";
//		GameDifficulty = ERPGGameDifficulty::Normal;
//		ContentNameText->SetText(FText::FromString(TEXT("SurvivalMode(Normal)")));
//		break;
//	case ERPGGameDifficulty::Hard:
//		RowName = "HardReward";
//		GameDifficulty = ERPGGameDifficulty::Hard;
//		ContentNameText->SetText(FText::FromString(TEXT("SurvivalMode(Hard)")));
//		break;
//	case ERPGGameDifficulty::Hell:
//		RowName = "HellReward";
//		GameDifficulty = ERPGGameDifficulty::Hell;
//		ContentNameText->SetText(FText::FromString(TEXT("SurvivalMode(Hell)")));
//		break;
//	default:
//
//		break;
//	}
//
//	// ���� ���̺����� Row ã�Ƽ� UI ������Ʈ
//	if (RewardDataTable)
//	{
//		const FItemDropTable* RewardData = RewardDataTable->FindRow<FItemDropTable>(RowName, RowName.ToString());
//
//		if (!RewardData)
//		{
//			UE_LOG(LogTemp, Warning, TEXT("���� �����͸� ã�� �� �����ϴ�: %s"), *RowName.ToString());
//			return;
//		}
//
//		TArray<FName> ItemRowNames;
//		TArray<int32> ItemQuantities;
//
//		const TArray<FRewardItem>& Rewards = RewardData->RewardItemList;
//
//		for (const FRewardItem& Reward : Rewards)
//		{
//			UE_LOG(LogTemp, Warning, TEXT("���� ������: %s, ����: %d, Ȯ��: %f"),
//				*Reward.ItemRowName.ToString(), Reward.DropQuantity, Reward.DropChance);
//			ItemRowNames.Add(Reward.ItemRowName);
//			ItemQuantities.Add(Reward.DropQuantity);
//		}
//
//		TArray<URPGItemBase*> RewardItems;
//
//		for (int32 i = 0; i < ItemRowNames.Num(); i++)
//		{
//			const FRPGItemData* ItemData = ItemDataTable->FindRow<FRPGItemData>(ItemRowNames[i], ItemRowNames[i].ToString());
//
//			if (ItemData)
//			{
//				URPGItemBase* RewardItem = NewObject<URPGItemBase>();
//
//				
//			}
//		}
//
//		for (URPGItemBase* Item : RewardItems)
//		{
//			if (!Item) continue;
//
//			// ���� UI ����
//			URPGRewardItemWidget* RewardWidget = CreateWidget<URPGRewardItemWidget>(this, RewardItemWidgetClass);
//			if (RewardWidget)
//			{
//				RewardWidget->SetItemReference(Item);
//				RewardInfoPanel->AddChildToWrapBox(RewardWidget);
//			}
//		}
//	}
//}

//void URPGContentEntryWidget::CreateAndAddRewardItemWidget(const FRPGItemData& ItemData, int32 Quantity)
//{
//
//}

void URPGContentEntryWidget::OnEntryButtonClicked()
{
	URPGMessageBox* MessageBox = CreateWidget<URPGMessageBox>(GetWorld(), MessageBoxClass);
	
	if (MessageBox)
	{
		MessageBox->SetGameLevelTag(LevelTag);

		MessageBox->AddToViewport();
	}
	
	URPGGameInstance* GI = URPGFunctionLibrary::GetRPGGameInstance(GetWorld());

	if (GI)
	{
		GI->SetPendingGameDifficulty(GameDifficulty);
	}
}

void URPGContentEntryWidget::OnCloseButtonClicked()
{
	UUIManager::Get()->RemoveUI(this);

	URPGFunctionLibrary::ToggleInputMode(GetWorld(), ERPGInputMode::GameOnly);
}
