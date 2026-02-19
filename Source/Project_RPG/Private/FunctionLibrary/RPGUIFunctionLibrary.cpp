// Fill out your copyright notice in the Description page of Project Settings.

#include "FunctionLibrary/RPGUIFunctionLibrary.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "Component/RPGInventoryComponent.h"
#include "UI/Inventory/RPGInventoryBase.h"
#include "UI/Inventory/Spatial/RPGInventoryGrid.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Components/Widget.h"
#include "Blueprint/SlateBlueprintLibrary.h"

FVector2D URPGUIFunctionLibrary::GetClampedWidgetPosition(const FVector2D& Boundary, const FVector2D& WidgetSize, const FVector2D& MousePos)
{
	FVector2D ClampedPosition = MousePos;
	ClampedPosition.X = FMath::Clamp(MousePos.X, 0.f, Boundary.X - WidgetSize.X);
	ClampedPosition.Y = FMath::Clamp(MousePos.Y, 0.f, Boundary.Y - WidgetSize.Y);
	return ClampedPosition;
}

int32 URPGUIFunctionLibrary::GetIndexFromWidgetPosition(const FIntPoint& Position, const int32 Columns)
{
	return Position.X + Position.Y * Columns;
}

FIntPoint URPGUIFunctionLibrary::GetPositionFromWidgetIndex(const int32 Index, const int32 Columns)
{
	return FIntPoint(Index % Columns, Index / Columns);
}

URPGHoverItem* URPGUIFunctionLibrary::GetHoverItem(APlayerController* PC)
{
	if (!IsValid(PC)) return nullptr;
	URPGInventoryComponent* IC = URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC);
	if (!IC) return nullptr;

	URPGInventoryBase* InventoryBase = IC->GetInventoryMenu();
	return IsValid(InventoryBase) ? InventoryBase->GetHoverItem() : nullptr;
}

EItemCategory URPGUIFunctionLibrary::GetItemCategoryFromItemPickup(ARPGPickUpBase* ItemPickup)
{
	return IsValid(ItemPickup) ? ItemPickup->GetItemManifest().GetItemCategory() : EItemCategory::None;
}

FVector2D URPGUIFunctionLibrary::GetWidgetPosition(UWidget* Widget)
{
	if (!Widget) return FVector2D::ZeroVector;
	const FGeometry Geometry = Widget->GetCachedGeometry();
	FVector2D PixelPosition, ViewportPosition;
	USlateBlueprintLibrary::LocalToViewport(Widget, Geometry, USlateBlueprintLibrary::GetLocalTopLeft(Geometry), PixelPosition, ViewportPosition);
	return ViewportPosition;
}

FVector2D URPGUIFunctionLibrary::GetWidgetSize(UWidget* Widget)
{
	return Widget ? Widget->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
}

bool URPGUIFunctionLibrary::IsWithinBounds(const FVector2D& BoundaryPos, const FVector2D& WidgetSize, const FVector2D& MousePos)
{
	return MousePos.X >= BoundaryPos.X && MousePos.X <= (BoundaryPos.X + WidgetSize.X) &&
		MousePos.Y >= BoundaryPos.Y && MousePos.Y <= (BoundaryPos.Y + WidgetSize.Y);
}

void URPGUIFunctionLibrary::ItemHovered(APlayerController* PC, URPGItemBase* Item)
{
	if (!IsValid(PC)) return;
	URPGInventoryComponent* InvenComp = URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC);
	if (!IsValid(InvenComp)) return;

	URPGInventoryBase* InventoryBase = InvenComp->GetInventoryMenu();
	if (!IsValid(InventoryBase) || InventoryBase->HasHoverItem()) return;

	InventoryBase->OnItemHovered(Item);
}

void URPGUIFunctionLibrary::ItemUnhovered(APlayerController* PC)
{
	if (!IsValid(PC)) return;
	URPGInventoryComponent* InvenComp = URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC);
	if (!IsValid(InvenComp)) return;

	URPGInventoryBase* InventoryBase = InvenComp->GetInventoryMenu();
	if (IsValid(InventoryBase)) InventoryBase->OnItemUnHovered();
}
