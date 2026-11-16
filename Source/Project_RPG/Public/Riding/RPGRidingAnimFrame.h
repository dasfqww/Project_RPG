#pragma once

#include "Riding/RPGRidingTypes.h"
#include "Templates/SubclassOf.h"
#include "RPGRidingAnimFrame.generated.h"

class UAnimInstance;
class USkeletalMeshComponent;
struct FRPGRidingPoseView;

/** Game-thread snapshot for the rider's authored animation graph. Never moves a component. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingAnimFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bHasAnchor = false;
    /** Saddle + RiderOffset expressed in the primary rider mesh's component space. */
    UPROPERTY(BlueprintReadOnly) FTransform RiderComponentTransform;
    UPROPERTY(BlueprintReadOnly) TSubclassOf<UAnimInstance> PoseLayer;
    UPROPERTY(BlueprintReadOnly) FRPGRidingStateKey Key;
};

namespace RPGRidingAnim
{
    /** Value conversion only; missing/invalid data returns a completely empty frame. */
    PROJECT_RPG_API FRPGRidingAnimFrame MakeFrame(const FRPGRidingPoseView& View, const FTransform& MeshWorld);
    /** Call on the game thread, for the character's primary mesh only. */
    PROJECT_RPG_API FRPGRidingAnimFrame Capture(const USkeletalMeshComponent* RiderMesh);
}
