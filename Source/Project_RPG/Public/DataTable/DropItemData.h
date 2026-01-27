#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Type/RPGStructTypes.h"
#include "DropItemData.generated.h"

class URPGItemBase;

USTRUCT(BlueprintType)
struct FItemDropTable:public FTableRowBase
{
	GENERATED_BODY()
public:
    // 드롭될 아이템 목록
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Table")
    TArray<FDropItem> DropItemList;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward Table")
    TArray<FRewardItem> RewardItemList;
};