#pragma once

#include "UI/Option/RPGOptionsMenuRuntime.h"

class FRPGOptionsMenuRuntime final : public IRPGOptionsMenuRuntime
{
public:
    virtual ERPGOptionsNavigationCommand GetNavigationCommand(const FKeyEvent& Event) const override;
    virtual bool CanNavigate(const URPGOptionMenu* Widget, const FRPGOptionsMenuInputState& State) const override;
    virtual bool AcquireInput(URPGOptionMenu* Widget, FRPGOptionsMenuInputState& State) override;
    virtual void ReleaseInput(URPGOptionMenu* Widget, FRPGOptionsMenuInputState& State,
        bool bRestoreInput = true) override;

private:
    TMap<TWeakObjectPtr<UGameViewportClient>, TWeakPtr<FRPGOptionsViewportInputState>> ViewportStates;
};
