#include "Riding/RPGRidingWorldPolicy.h"
#include "Riding/RPGRidingRegionVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"

namespace
{
    struct FInterval
    {
        double Enter = 0, Exit = 1;
        bool bDeny = false;
        bool bGrantsAllowance = true;
        FGameplayTagContainer Tags;
    };
    bool IntersectBox(const FVector& Start, const FVector& Delta, const FVector& Extent, double& Enter, double& Exit)
    {
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (Extent[Axis] < 0) return false;
            if (FMath::Abs(Delta[Axis]) < UE_DOUBLE_SMALL_NUMBER)
            { if (FMath::Abs(Start[Axis]) > Extent[Axis]) return false; continue; }
            double A = (-Extent[Axis] - Start[Axis]) / Delta[Axis];
            double B = (Extent[Axis] - Start[Axis]) / Delta[Axis];
            if (A > B) Swap(A, B);
            Enter = FMath::Max(Enter, A); Exit = FMath::Min(Exit, B);
            if (Enter > Exit) return false;
        }
        return true;
    }
    bool AllowedAt(double Time, const TArray<FInterval>& Regions, const FGameplayTagContainer& MountTags)
    {
        bool bAllowed = false, bRestricted = false;
        FGameplayTagContainer Common;
        for (const auto& Region : Regions)
        {
            if (Time < Region.Enter || Time > Region.Exit) continue;
            if (Region.bDeny) return false;
            bAllowed |= Region.bGrantsAllowance;
            if (Region.Tags.IsEmpty()) continue;
            if (!bRestricted) { Common = Region.Tags; bRestricted = true; }
            else
            {
                FGameplayTagContainer Intersection;
                for (const auto& Tag : Common) if (Region.Tags.HasTagExact(Tag)) Intersection.AddTag(Tag);
                Common = MoveTemp(Intersection);
            }
        }
        return bAllowed && (!bRestricted || MountTags.HasAnyExact(Common));
    }
}

bool URPGRidingWorldPolicy::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

FRPGRidingPathPermission URPGRidingWorldPolicy::EvaluatePath(const ACharacter& Rider, const FVector& Start,
    const FVector& End, const FGameplayTagContainer& MountTags) const
{
    const UWorld* World = GetWorld();
    const auto* Capsule = Rider.GetCapsuleComponent();
    if (!IsInGameThread() || !World || World->bIsTearingDown || World->GetNetMode() == NM_Client
        || World->IsVisibilityRequestPending() || !World->AreAlwaysLoadedLevelsLoaded()
        || Rider.GetWorld() != World || !Rider.HasAuthority() || !Capsule || Capsule->GetOwner() != &Rider
        || Start.ContainsNaN() || End.ContainsNaN()) return {};
    const double Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0 || HalfHeight < Radius) return {};
    TArray<FInterval> Regions;
    TArray<double> Boundaries{0, 1};
    int32 Count = 0;
    for (TActorIterator<ARPGRidingRegionVolume> It(const_cast<UWorld*>(World)); It; ++It)
    {
        const auto* Region = *It;
        if (Region->IsActorBeingDestroyed()) continue;
        if (++Count > 128 || !Region->HasAuthority() || !IsValid(Region->Bounds)
            || !Region->Bounds->IsRegistered() || Region->AllowMountTags.Num() > 64) return {};
        const auto* Box = Region->Bounds.Get();
        const FQuat Rotation = Box->GetComponentQuat();
        const FVector Center = Box->GetComponentLocation(), Size = Box->GetScaledBoxExtent();
        if (Center.ContainsNaN() || Size.ContainsNaN() || Rotation.ContainsNaN() || !Rotation.IsNormalized()
            || Size.GetMin() <= 0) return {};
        // Exact capsule support along each box axis; Deny uses its conservative box envelope.
        const FVector Axis = Rotation.UnrotateVector(Capsule->GetUpVector()).GetAbs();
        const FVector Support = FVector(Radius) + Axis * (HalfHeight - Radius);
        const FVector LocalStart = Rotation.UnrotateVector(Start - Center);
        const FVector LocalDelta = Rotation.UnrotateVector(End - Start);
        FInterval Interval; Interval.bDeny = Region->IsDenyRegion();
        const FVector Extent = Interval.bDeny ? Size + Support : Size - Support;
        if (IntersectBox(LocalStart, LocalDelta, Extent, Interval.Enter, Interval.Exit))
        { Regions.Add(Interval); Boundaries.Add(Interval.Enter); Boundaries.Add(Interval.Exit); }
        // A restriction applies as soon as any of the capsule touches it, even beside a larger unrestricted Allow.
        if (!Interval.bDeny && !Region->AllowMountTags.IsEmpty())
        {
            FInterval Restriction; Restriction.bGrantsAllowance = false; Restriction.Tags = Region->AllowMountTags;
            if (IntersectBox(LocalStart, LocalDelta, Size + Support, Restriction.Enter, Restriction.Exit))
            { Regions.Add(Restriction); Boundaries.Add(Restriction.Enter); Boundaries.Add(Restriction.Exit); }
        }
    }
    if (!AllowedAt(0, Regions, MountTags)) return {};
    Boundaries.Sort();
    for (int32 Index = 1; Index < Boundaries.Num(); ++Index)
    {
        const double Previous = Boundaries[Index - 1], Current = Boundaries[Index];
        if (!AllowedAt((Previous + Current) * 0.5, Regions, MountTags)) return {false, Previous};
        if (!AllowedAt(Current, Regions, MountTags)) return {false, Current};
    }
    return {true, 1};
}
