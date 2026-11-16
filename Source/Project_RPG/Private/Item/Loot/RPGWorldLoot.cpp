#include "Item/Loot/RPGWorldLoot.h"
#include "DataTable/DropItemData.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Component/RPGItemCommandComponent.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "RPGGameplayTags.h"
#include "Riding/RPGRidingComponent.h"

bool RPGWorldLoot::Validate(const TArray<FDropItem>& Drops)
{
    if (Drops.Num() > 32) return false;
    for (const auto& Drop : Drops)
    {
        if (!FMath::IsFinite(Drop.DropChance) || Drop.DropChance < 0 || Drop.DropChance > 1) return false;
        if (Drop.DropChance == 0) continue;
        const auto* Defaults = Drop.ItemClass ? Drop.ItemClass->GetDefaultObject<ARPGPickUpBase>() : nullptr;
        if (!IsValid(Defaults) || Drop.ItemClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) ||
            !Defaults->CanInitializeServerDropQuantity(Drop.DropQuantity)) return false;
    }
    return true;
}
bool RPGWorldLoot::ReadDrops(const UDataTable* Table, FName Row, TArray<FDropItem>& Out)
{
    Out.Reset();
    if (!IsValid(Table) || Row.IsNone() || Table->GetRowStruct() != FItemDropTable::StaticStruct()) return false;
    const auto* Data = Table->FindRow<FItemDropTable>(Row, TEXT("World loot"), false);
    if (!Data || !Validate(Data->DropItemList)) return false;
    Out = Data->DropItemList; return true;
}
int32 RPGWorldLoot::Spawn(AActor& Source, const TArray<FDropItem>& FrozenDrops)
{
    UWorld* World = Source.GetWorld();
    if (!Source.HasAuthority() || !World || World->bIsTearingDown || Source.IsActorBeingDestroyed() ||
        Source.GetActorLocation().ContainsNaN() || !Validate(FrozenDrops)) return 0;
    // Own a copy across Construction/BeginPlay and actor-spawn callbacks.
    const auto Drops = FrozenDrops;
    const FVector Start = Source.GetActorLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WorldLootLanding), false, &Source);
    const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    FHitResult BaseHit;
    const bool bGround = World->LineTraceSingleByObjectType(BaseHit, Start + FVector::UpVector * 100.f,
        Start - FVector::UpVector * 300.f, Objects, Query);
    const FVector Base = bGround ? BaseHit.ImpactPoint : Start;
    const float Yaw = FMath::FRandRange(0.f, 2.f * PI);
    int32 Count = 0, Index = 0;
    for (const auto& Drop : Drops)
    {
        if (!IsValid(&Source) || Source.IsActorBeingDestroyed()) break;
        if (Drop.DropChance == 0 || (Drop.DropChance < 1 && FMath::FRand() >= Drop.DropChance)) continue;
        const int32 Seed = Index++;
        const float Angle = Yaw + Seed * 2.39996323f;
        const float Radius = FMath::Min(180.f, 65.f + 22.f * FMath::Sqrt(static_cast<float>(Seed)));
        const FVector Candidate = Base + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
        FVector Landing = Base;
        FHitResult Ground, Wall;
        if (bGround && World->LineTraceSingleByObjectType(Ground, Candidate + FVector::UpVector * 120.f,
            Candidate - FVector::UpVector * 250.f, Objects, Query) && Ground.ImpactNormal.Z >= .65f &&
            !World->LineTraceSingleByObjectType(Wall, Base + FVector::UpVector * 35.f,
                Ground.ImpactPoint + FVector::UpVector * 35.f, Objects, Query)) Landing = Ground.ImpactPoint;
        bool bReady = false;
        FActorSpawnParameters Params;
        Params.CustomPreSpawnInitialization = [&bReady, Quantity=Drop.DropQuantity, Origin=Start+FVector::UpVector*20.f, Seed](AActor* A)
        {
            auto* P = CastChecked<ARPGPickUpBase>(A);
            bReady = P->InitializeServerDropQuantity(Quantity);
            if (bReady) P->InitializeDropPresentation(Origin, Seed);
            else P->TryClaimPickup();
        };
        auto* Pickup = World->SpawnActor<ARPGPickUpBase>(Drop.ItemClass, Landing + FVector::UpVector * 5.f,
            FRotator::ZeroRotator, Params);
        if (IsValid(Pickup)) { if (bReady) ++Count; else Pickup->Destroy(); }
    }
    return Count;
}
ARPGPlayerState* RPGWorldLoot::Collector(const AActor& Target, APlayerController* Controller, const float Distance)
{
    const APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
    auto* State = IsValid(Controller) ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
    auto* Commands = IsValid(Controller) ? Controller->FindComponentByClass<URPGItemCommandComponent>() : nullptr;
    FRPGItemOwnerRef Owner;
    Owner.Type = ERPGItemOwnerType::Character;
    if (State) Owner.OwnerId = State->GetBackendCharacterId();
    if (!Target.HasAuthority() || Target.IsActorBeingDestroyed() || Target.IsHidden() || !Target.GetWorld() || Target.GetWorld()->bIsTearingDown ||
        !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Controller->IsActorBeingDestroyed() ||
        Pawn->GetWorld() != Target.GetWorld() || Controller->GetWorld() != Target.GetWorld() || !IsValid(State) ||
        !State->HasAuthenticatedCharacter() || !Commands ||
        !Commands->IsAuthenticatedItemOwner(Owner) || IsDungeonGameplayRevoked(Pawn) ||
        !FMath::IsFinite(Distance) || Distance <= 0 || Target.GetActorLocation().ContainsNaN() || Pawn->GetActorLocation().ContainsNaN() ||
        FVector::DistSquared(Pawn->GetActorLocation(), Target.GetActorLocation()) > FMath::Square(Distance)) return nullptr;
    const auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true);
    if (ASC && ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)) return nullptr;
    const auto* Riding = Pawn->FindComponentByClass<URPGRidingComponent>();
    if (Riding && Riding->GetRidingState().Mode != ERPGRidingMode::OnFoot) return nullptr;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WorldLootAccess), false, Pawn);
    Query.AddIgnoredActor(&Target);
    FHitResult Hit;
    if (Target.GetWorld()->LineTraceSingleByObjectType(Hit, Pawn->GetActorLocation(), Target.GetActorLocation(),
        FCollisionObjectQueryParams(ECC_WorldStatic), Query)) return nullptr;
    return State;
}
