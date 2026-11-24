#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPGItemQuickSlotBar.generated.h"

class URPGQuickSlotWidget;
class URPGRidingCollectionWidget;

/** Right-hand HUD: a health-potion cell and eight item or mount shortcuts. */
UCLASS()
class PROJECT_RPG_API URPGItemQuickSlotBar : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    int32 GetCellCount() const { return Cells.Num(); }
    URPGQuickSlotWidget* GetCell(int32 Index) const { return Cells.IsValidIndex(Index) ? Cells[Index].Get() : nullptr; }
    void OpenMountCollection();
    void ToggleMountCollection();
    bool IsMountCollectionOpen() const;
protected:
    virtual void NativeDestruct() override;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<URPGQuickSlotWidget>> Cells;
    UPROPERTY(Transient) TObjectPtr<URPGRidingCollectionWidget> MountCollection;
};
