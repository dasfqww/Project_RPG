#pragma once

#include "CoreMinimal.h"

class APlayerController;
class URPGRidingSessionComponent;

namespace RPGMountQuickSlots
{
    bool IsMountSlot(int32 Index);
    const URPGRidingSessionComponent* FindSession(const APlayerController* Controller);
    FString Read(const APlayerController* Controller, int32 Index);
    bool Assign(const APlayerController* Controller, int32 Index, const FString& MountId);
}
