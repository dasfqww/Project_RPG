// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Type/RPGEnumTypes.h"
#include "RPGUIFunctionLibrary.generated.h"

class URPGItemBase;
class ARPGPickUpBase;
class UWidget;
class URPGHoverItem;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGUIFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "RPG|UIFunctionLibrary")
	static FVector2D GetClampedWidgetPosition(const FVector2D& Boundary,
		const FVector2D& WidgetSize, const FVector2D& MousePos);

	UFUNCTION(BlueprintCallable, Category = "RPG|UIFunctionLibrary")
	static int32 GetIndexFromWidgetPosition(const FIntPoint& Position, const int32 Columns);

	UFUNCTION(BlueprintCallable, Category = "RPG|UIFunctionLibrary")
	static FIntPoint GetPositionFromWidgetIndex(const int32 Index, const int32 Columns);

	UFUNCTION(BlueprintCallable, Category = "RPG|UIFunctionLibrary")
	static URPGHoverItem* GetHoverItem(APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "RPG|UIFunctionLibrary")
	static EItemCategory GetItemCategoryFromItemPickup(ARPGPickUpBase* ItemPickup);

	UFUNCTION(BlueprintCallable, Category = "RPG|UIFunctionLibrary")
	static FVector2D GetWidgetPosition(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "RPG|UIFunctionLibrary")
	static FVector2D GetWidgetSize(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "RPG|UIFunctionLibrary")
	static bool IsWithinBounds(const FVector2D& BoundaryPos, 
		const FVector2D& WidgetSize, const FVector2D& MousePos);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemHovered(APlayerController* PC, URPGItemBase* Item);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemUnhovered(APlayerController* PC);

	template<typename T, typename FuncT>
	static void ForeachGridSlot2D(TArray<T>& Array, int32 Index,
		const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function);
};

template<typename T, typename FuncT>
inline void URPGUIFunctionLibrary::ForeachGridSlot2D(TArray<T>& Array, int32 Index,
	const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function)
{
	for (int32 j = 0; j < Range2D.Y; ++j)
	{
		for (int32 i = 0; i < Range2D.X; ++i)
		{
			const FIntPoint Coordinates = GetPositionFromWidgetIndex(Index, GridColumns) + FIntPoint(i, j);
			const int32 TileIndex = GetIndexFromWidgetPosition(Coordinates, GridColumns);
			if (Array.IsValidIndex(TileIndex))
			{
				Function(Array[TileIndex]);
			}
		}
	}
}
