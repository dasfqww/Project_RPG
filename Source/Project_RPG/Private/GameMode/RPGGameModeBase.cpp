// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/RPGGameModeBase.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGInventoryComponent.h"
#include "DataTable/DropItemData.h"
#include "Kismet/GameplayStatics.h"
#include "UI/RPGContentClearPanel.h"
#include "GameInstance/RPGGameInstance.h"

#include "RPGDebugHelper.h"

ARPGGameModeBase::ARPGGameModeBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ARPGGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	//마을이라거나 보상이 없는 맵은 제외한다.
	
	if (bGiveReward)
	{
		//InitializeRewardItems();
		if (URPGGameInstance* GI = GetGameInstance<URPGGameInstance>())
		{
			SetGameDifficulty(GI->GetPendingGameDifficulty());
		}
	}
}

void ARPGGameModeBase::SetGameDifficulty(ERPGGameDifficulty InGameDifficulty)
{
	switch (InGameDifficulty)
	{
	case ERPGGameDifficulty::Easy:
		CurrentGameDifficulty = ERPGGameDifficulty::Easy;
		InitializeRewardItems("EasyReward");
		break;
	case ERPGGameDifficulty::Normal:
		CurrentGameDifficulty = ERPGGameDifficulty::Normal;
		InitializeRewardItems("NormalReward");
		break;
	case ERPGGameDifficulty::Hard:
		CurrentGameDifficulty = ERPGGameDifficulty::Hard;
		InitializeRewardItems("HardReward");
		break;
	case ERPGGameDifficulty::Hell:
		CurrentGameDifficulty = ERPGGameDifficulty::Hell;
		InitializeRewardItems("HellReward");
		break;
	default:
		break;
	}
	
}

void ARPGGameModeBase::GiveContentReward(ARPGPlayer* Player)
{
	//if (!RewardDataTable || !Player) return;

	//URPGInventoryComponent* PlayerInventory = Player->GetRPGInventory();

	//if (!PlayerInventory) return;

	////TODO:인벤토리에 아이템 지급

	//if (ClearPanelClass)
	//{
	//	URPGContentClearPanel* ClearPanel = CreateWidget<URPGContentClearPanel>(GetWorld(), ClearPanelClass);

	//	if (ClearPanel)
	//	{
	//		ClearPanel->AddToViewport();
	//		ClearPanel->SetRenderTranslation(FVector2D(CoordX, 0.f));
	//	}
	//}

	//for (URPGItemBase* RewardItem : RewardItems)
	//{
	//	if (RewardItem)
	//	{
	//		const FItemAddResult AddResult = PlayerInventory->HandleAddItem(RewardItem);

	//		switch (AddResult.OperationResult)
	//		{
	//		case EItemAddResult::IAR_NoItemAdded:
	//			break;
	//		case EItemAddResult::IAR_PartialAmountItemAdded:

	//			break;
	//		case EItemAddResult::IAR_AllItemAdded:
	//			Destroy();
	//			break;
	//		}

	//		UE_LOG(LogTemp, Warning, TEXT("%s"), *AddResult.ResultMessage.ToString());
	//	}

	//	else
	//	{
	//		Debug::Print("Player Inventory component is null..");
	//	}
	//}
}

void ARPGGameModeBase::InitializeRewardItems(FName InName)
{	
	//FName RowName("SurvivalReward");

	//FString ContextString = TEXT("Reward Lookup");

	/*const FItemDropTable* RewardData = RewardDataTable->FindRow<FItemDropTable>(InName, InName.ToString());

	if (!RewardData)
	{
		UE_LOG(LogTemp, Warning, TEXT("보상 데이터를 찾을 수 없습니다: %s"), *InName.ToString());
		return;
	}

	TArray<FName> ItemRowNames;
	TArray<int32> ItemQuantities;

	const TArray<FRewardItem>& Rewards = RewardData->RewardItemList;

	for (const FRewardItem& Reward : Rewards)
	{
		UE_LOG(LogTemp, Warning, TEXT("보상 아이템: %s, 수량: %d, 확률: %f"),
			*Reward.ItemRowName.ToString(), Reward.DropQuantity, Reward.DropChance);
		ItemRowNames.Add(Reward.ItemRowName);
		ItemQuantities.Add(Reward.DropQuantity);
	}
	
	for (int32 i = 0; i < ItemRowNames.Num(); i++)
	{
		const FRPGItemData* ItemData = ItemDataTable->FindRow<FRPGItemData>(ItemRowNames[i], ItemRowNames[i].ToString());
	
		if (ItemData)
		{
			URPGItemBase* RewardItem = NewObject<URPGItemBase>();

			

			RewardItems.Add(RewardItem);
		}
	}*/
}
