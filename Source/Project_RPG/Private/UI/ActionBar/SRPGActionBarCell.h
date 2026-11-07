#pragma once

#include "UI/ActionBar/RPGActionBarActionView.h"
#include "Widgets/SCompoundWidget.h"

/** The enabled outer cell keeps its tooltip reachable when its action button is disabled. */
class SRPGActionBarCell : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRPGActionBarCell) : _Icon(nullptr), _IsHeld(false) {}
        SLATE_ARGUMENT(FText, Label)
        SLATE_ARGUMENT(const FSlateBrush*, Icon)
        SLATE_ATTRIBUTE(FRPGActionBarActionView, View)
        SLATE_ATTRIBUTE(bool, IsHeld)
        SLATE_EVENT(FSimpleDelegate, OnPressed)
        SLATE_EVENT(FSimpleDelegate, OnReleased)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    FText Label;
    TAttribute<FRPGActionBarActionView> View;
    TAttribute<bool> IsHeld;
};
