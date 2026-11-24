#pragma once

#include "Input/DragAndDrop.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FSlateDynamicImageBrush;
class URPGRidingBrowseButton;

/** A mount portrait that is centered on the pointer from the first drag frame. */
class FRPGMountDragDrop final : public FGameDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FRPGMountDragDrop, FGameDragDropOperation)

    FRPGMountDragDrop(URPGRidingBrowseButton& InRow, TSharedRef<FSlateDynamicImageBrush> InPortrait,
        float ViewportScale, const FPointerEvent& Event);

    const URPGRidingBrowseButton* GetRow() const;
    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return Decorator; }
    virtual bool AffectedByPointerEvent(const FPointerEvent& Event) override;
    virtual void OnDragged(const FDragDropEvent& Event) override;

private:
    void PositionAt(const FPointerEvent& Event);

    TWeakObjectPtr<URPGRidingBrowseButton> Row;
    TSharedRef<FSlateDynamicImageBrush> Portrait;
    TSharedPtr<SWidget> Decorator;
    int32 PointerIndex;
};
