#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/WeakObjectPtr.h"
#include "RPGRidingDashMotionHost.generated.h"

class UActorComponent;
struct FRPGRidingDashWindow
{
    TWeakObjectPtr<UActorComponent> Owner;
    FGuid Handle;
    TFunction<void()> Close;
    void Release() const { if (Close) Close(); }
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class URPGRidingDashMotionHost : public UInterface { GENERATED_BODY() };

/** Native Player CMC adapter; no BP/RPC authority setters. Retain the exact window owner as well as its handle. */
class PROJECT_RPG_API IRPGRidingDashMotionHost
{
    GENERATED_BODY()
public:
    virtual bool CanOpenRidingDashWindow() const = 0;
    virtual FRPGRidingDashWindow OpenRidingDashWindow(float Duration, float Distance) = 0;
};
