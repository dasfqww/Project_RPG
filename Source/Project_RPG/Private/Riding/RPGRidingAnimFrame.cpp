#include "Riding/RPGRidingAnimFrame.h"
#include "Riding/RPGRidingPresentationComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace
{
    bool UsableTransform(const FTransform& Transform)
    {
        const FVector Scale = Transform.GetScale3D();
        return Transform.IsValid() && Scale.X > SMALL_NUMBER && Scale.Y > SMALL_NUMBER && Scale.Z > SMALL_NUMBER;
    }
}

FRPGRidingAnimFrame RPGRidingAnim::MakeFrame(const FRPGRidingPoseView& View, const FTransform& MeshWorld)
{
    if (!View.bHasAnchor || !View.Key.RiderEpoch.IsValid() || View.Key.StateRevision == 0 || !View.PoseLayer
        || !UsableTransform(View.RiderWorldTransform) || !UsableTransform(MeshWorld)) return {};
    const FTransform Component = View.RiderWorldTransform.GetRelativeTransform(MeshWorld);
    if (!UsableTransform(Component)) return {};
    FRPGRidingAnimFrame Frame;
    Frame.bHasAnchor = true;
    Frame.RiderComponentTransform = Component;
    Frame.PoseLayer = View.PoseLayer;
    Frame.Key = View.Key;
    return Frame;
}

FRPGRidingAnimFrame RPGRidingAnim::Capture(const USkeletalMeshComponent* RiderMesh)
{
    if (!IsInGameThread() || !IsValid(RiderMesh) || !RiderMesh->IsRegistered()) return {};
    const auto* Character = Cast<ACharacter>(RiderMesh->GetOwner());
    const auto* World = RiderMesh->GetWorld();
    if (!IsValid(Character) || Character->IsActorBeingDestroyed() || Character->GetMesh() != RiderMesh
        || !RiderMesh->GetSkeletalMeshAsset() || !IsValid(World) || !World->IsGameWorld()
        || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer) return {};
    const auto* Presentation = Character->FindComponentByClass<URPGRidingPresentationComponent>();
    return IsValid(Presentation)
        ? MakeFrame(Presentation->GetPoseView(), RiderMesh->GetComponentTransform()) : FRPGRidingAnimFrame{};
}
