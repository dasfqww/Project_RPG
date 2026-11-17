#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPGItemQuickSlotBar.generated.h"

class URPGQuickSlotWidget;

/** Right-hand item HUD: a health-potion cell and eight independent consumable cells. */
UCLASS()
class PROJECT_RPG_API URPGItemQuickSlotBar : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    int32 GetCellCount() const { return Cells.Num(); }
    URPGQuickSlotWidget* GetCell(int32 Index) const { return Cells.IsValidIndex(Index) ? Cells[Index].Get() : nullptr; }
private:
    UPROPERTY(Transient) TArray<TObjectPtr<URPGQuickSlotWidget>> Cells;
};
