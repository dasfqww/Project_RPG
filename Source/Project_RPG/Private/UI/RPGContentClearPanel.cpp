// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGContentClearPanel.h"
#include "Components/SizeBox.h"
#include "GameMode/RPGGameModeBase.h"
#include "UI/RPGRewardItemWidget.h"
#include "Components/WrapBox.h"

URPGContentClearPanel::URPGContentClearPanel()
{

}

void URPGContentClearPanel::NativeConstruct()
{
	Super::NativeConstruct();

    // 게임모드 가져오기
    if (GetWorld())
    {
        GameModeReference = Cast<ARPGGameModeBase>(GetWorld()->GetAuthGameMode());
    }

    ShowContentClearReward();
}

//void URPGContentClearPanel::NativeOnInitialized()
//{
//
//}

void URPGContentClearPanel::ShowContentClearReward()
{
    if (!GameModeReference||!RewardItemWidgetClass) return;

    // 보상 아이템 목록 가져오기
    const TArray<URPGItemBase*>& RewardItems = GameModeReference->GetRewardItems();

    for (URPGItemBase* Item : RewardItems)
    {
        if (!Item) continue;

        // 보상 UI 생성
        URPGRewardItemWidget* RewardWidget = CreateWidget<URPGRewardItemWidget>(this, RewardItemWidgetClass);
        if (RewardWidget)
        {
            RewardWidget->SetItemReference(Item);
            RewardInfoPanel->AddChildToWrapBox(RewardWidget);
        }
    }
}