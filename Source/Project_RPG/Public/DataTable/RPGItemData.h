#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "RPGItemData.generated.h"

class UStaticMesh;
class UTexture2D;

/** Legacy item type retained so existing item data tables can be loaded safely. */
UENUM(BlueprintType)
enum class EItemType : uint8
{
	Armor,
	Weapon,
	Comsumable UMETA(DisplayName = "Consumable"),
	Quest,
	Mundane
};

/** Legacy item grade retained so existing item data tables can be migrated. */
UENUM(BlueprintType)
enum class EItemGrade : uint8
{
	Common,
	Advanced,
	Rare,
	Hero,
	Legend
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FItemStatistics
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	float Attack = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	float Defense = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	float RestorationAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	float SellValue = 0.0f;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FItemTextData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FText InteractionText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FText UsageText;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FItemNumericData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	float Weight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data", meta = (ClampMin = "1"))
	int32 MaxStackSize = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	bool bIsStackable = false;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FItemAssetData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	TObjectPtr<UStaticMesh> Mesh;
};

/**
 * Compatibility row for DT_ItemInfo.
 *
 * ItemStstistics intentionally preserves the historical misspelling serialized
 * in the asset. A later migration can move these rows to the item-manifest model.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	EItemType ItemType = EItemType::Mundane;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	EItemGrade ItemGrade = EItemGrade::Common;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FItemStatistics ItemStstistics;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FItemTextData ItemTextData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FItemNumericData ItemNumericData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Data")
	FItemAssetData ItemAssetData;
};
