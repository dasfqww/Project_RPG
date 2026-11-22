#pragma once

#include "CoreMinimal.h"

class SHorizontalBox;
class SWidget;

namespace RPGActionBarLayout
{
inline constexpr float CellWidth = 64.f;
inline constexpr float CellHeight = 64.f;

/** Output maps display positions to action indices; INDEX_NONE denotes a noninteractive gap. */
bool Resolve(TConstArrayView<int32> DisplaySlots, int32 FixedSlotCount, TArray<int32>& OutOrder);
TSharedRef<SHorizontalBox> CreateRow(TConstArrayView<int32> Order,
    TFunctionRef<TSharedRef<SWidget>(int32)> CreateCell);
TSharedRef<SWidget> CreateRows(TConstArrayView<int32> Order, int32 ColumnCount,
    TFunctionRef<TSharedRef<SWidget>(int32)> CreateCell);
}
