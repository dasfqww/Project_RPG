#pragma once

#include "CoreMinimal.h"
#include "Item/RPGInventoryTypes.h"
#include "UObject/PrimaryAssetId.h"

namespace RPGItemInventoryPolicy
{
// Persistent slot encoding: equipment [0,80), consumables [80,160), other [160,240).
inline constexpr int32 SlotsPerTab = 80;
inline constexpr int32 SlotCapacity = 3 * SlotsPerTab;
inline bool IsTab(EItemCategory Category)
{ return Category == EItemCategory::Equip || Category == EItemCategory::Consume || Category == EItemCategory::Craft; }
inline int32 FirstSlot(EItemCategory Category)
{ return IsTab(Category) ? static_cast<int32>(Category) * SlotsPerTab : INDEX_NONE; }
inline EItemCategory CategoryForDefinition(const FPrimaryAssetId& Definition)
{
	// Canonical item namespaces are shared with the backend. Unknown kinds belong to Other.
	const FString Name = Definition.PrimaryAssetName.ToString();
	if (Name.StartsWith(TEXT("GameItem.Equipment."), ESearchCase::CaseSensitive)) return EItemCategory::Equip;
	if (Name.StartsWith(TEXT("GameItem.Consume."), ESearchCase::CaseSensitive)) return EItemCategory::Consume;
	return EItemCategory::Craft;
}
inline bool Contains(EItemCategory Category, int32 Slot)
{ return IsTab(Category) && Slot >= FirstSlot(Category) && Slot < FirstSlot(Category) + SlotsPerTab; }
inline bool CanStore(const FPrimaryAssetId& Definition, int32 Slot)
{ return Contains(CategoryForDefinition(Definition), Slot); }
}
