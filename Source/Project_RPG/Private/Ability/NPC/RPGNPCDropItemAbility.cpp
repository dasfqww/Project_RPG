// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/NPC/RPGNPCDropItemAbility.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Item/RPGItemBase.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Type/RPGStructTypes.h"
#include "DataTable/DropItemData.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Item/Fragment/RPGItemFragment.h"

#include "RPGDebugHelper.h"

URPGNPCDropItemAbility::URPGNPCDropItemAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
}

bool URPGNPCDropItemAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    const auto* NPC = ActorInfo ? Cast<ARPGNonPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
    return IsValid(NPC) && NPC->CanProcessDeathItemDrop() &&
        NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::ServerOnly &&
        NetSecurityPolicy == EGameplayAbilityNetSecurityPolicy::ServerOnly &&
        (InstancingPolicy == EGameplayAbilityInstancingPolicy::InstancedPerActor ||
            InstancingPolicy == EGameplayAbilityInstancingPolicy::InstancedPerExecution) &&
        ActorInfo->AbilitySystemComponent.Get() == NPC->GetRPGAbilitySystemComponent() &&
        Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void URPGNPCDropItemAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, 
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SpawnItem();
	if (IsActive()) EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	//Debug::Print("DropItem activate..");

	//EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void URPGNPCDropItemAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);


}

void URPGNPCDropItemAbility::SpawnItem()
{
    auto* NPC = CurrentActorInfo ? Cast<ARPGNonPlayerCharacter>(CurrentActorInfo->AvatarActor.Get()) : nullptr;
    UWorld* World = IsValid(NPC) ? NPC->GetWorld() : nullptr;
    if (!World || !NPC->CanProcessDeathItemDrop() ||
        CurrentActorInfo->AbilitySystemComponent.Get() != NPC->GetRPGAbilitySystemComponent()) return;

    if (!IsValid(ItemDropTable) || DropRowName.IsNone() ||
        ItemDropTable->GetRowStruct() != FItemDropTable::StaticStruct())
    {
        UE_LOG(LogTemp, Warning, TEXT("NPC drop rejected: missing table/row or incompatible row type"));
        return;
    }

    const FItemDropTable* DropData = ItemDropTable->FindRow<FItemDropTable>(DropRowName, TEXT("NPC death drop"), false);
    if (!DropData || DropData->DropItemList.Num() > 32)
    {
        UE_LOG(LogTemp, Warning, TEXT("NPC drop rejected: missing row or more than 32 drop entries"));
        return;
    }
    // Freeze the complete row before any Blueprint spawn callback mutates its table.
    const TArray<FDropItem> Drops = DropData->DropItemList;
    for (const FDropItem& Drop : Drops)
    {
        if (!FMath::IsFinite(Drop.DropChance) || Drop.DropChance < 0.f || Drop.DropChance > 1.f)
        {
            UE_LOG(LogTemp, Warning, TEXT("NPC drop rejected: invalid probability"));
            return;
        }
        if (Drop.DropChance == 0.f) continue;
        const auto* Defaults = Drop.ItemClass ? Drop.ItemClass->GetDefaultObject<ARPGPickUpBase>() : nullptr;
        // The existing BP interaction quantity is int8. Reject unrepresentable
        // amounts instead of truncating the pickup display/grant contract.
        if (!IsValid(Defaults) || Drop.ItemClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) ||
            Drop.DropQuantity <= 0 || Drop.DropQuantity > MAX_int8)
        {
            UE_LOG(LogTemp, Warning, TEXT("NPC drop rejected: invalid pickup class or quantity"));
            return;
        }
        const FItemManifest Manifest = Defaults->GetItemManifest();
        const auto* Stack = Manifest.GetFragmentOfType<FStackableFragment>();
        if (!Manifest.GetItemTag().IsValid() || Manifest.GetItemCategory() == EItemCategory::None ||
            (Stack ? Stack->GetMaxQuantity() < 1 || Drop.DropQuantity > Stack->GetMaxQuantity() : Drop.DropQuantity != 1))
        {
            UE_LOG(LogTemp, Warning, TEXT("NPC drop rejected: invalid pickup manifest or stack quantity"));
            return;
        }
    }

    if (NPC->GetActorLocation().ContainsNaN() || !NPC->TryBeginDeathItemDrop()) return;

    FVector Start = NPC->GetActorLocation();
    FVector End = Start + (NPC->GetActorUpVector() * -200.0f);

    TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
    ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));

    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(NPC);

    FHitResult HitResult;
    bool bHit = UKismetSystemLibrary::LineTraceSingleForObjects(
        World,
        Start,
        End,
        ObjectTypes,
        false,
        ActorsToIgnore,
        EDrawDebugTrace::None,
        HitResult,
        true
    );

    FVector DropLocation = bHit ? HitResult.Location : Start;
    int32 DropIndex = 0;
    const float ScatterYaw = FMath::FRandRange(0.f, 2.f * PI);

    for (const FDropItem& DropItem : Drops)
    {
        if (!IsValid(NPC) || NPC->IsActorBeingDestroyed()) break;
        if (DropItem.DropChance == 1.f || (DropItem.DropChance > 0.f && FMath::FRand() < DropItem.DropChance))
        {
            // Spread real interaction anchors on reachable ground, never through a wall
            // or over an edge. The client animates only a cosmetic mesh to this point.
            const int32 Index = DropIndex++;
            const float Angle = ScatterYaw + Index * 2.39996323f;
            const float Radius = FMath::Min(180.f, 65.f + 22.f * FMath::Sqrt(static_cast<float>(Index)));
            const FVector Candidate = DropLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(LootLanding), false, NPC);
            FHitResult GroundHit, WallHit;
            FVector Landing = DropLocation;
            if (bHit && World->LineTraceSingleByObjectType(GroundHit, Candidate + FVector::UpVector * 120.f,
                Candidate - FVector::UpVector * 250.f, FCollisionObjectQueryParams(ECC_WorldStatic), Query) &&
                GroundHit.ImpactNormal.Z >= 0.65f &&
                !World->LineTraceSingleByObjectType(WallHit, DropLocation + FVector::UpVector * 35.f,
                    GroundHit.ImpactPoint + FVector::UpVector * 35.f, FCollisionObjectQueryParams(ECC_WorldStatic), Query))
                Landing = GroundHit.ImpactPoint;
            Landing += FVector::UpVector * 5.f;

            bool bQuantityInitialized = false;
            FActorSpawnParameters SpawnParameters;
            // Configure before Construction/BeginPlay/overlap or spawn callbacks
            // can request a pickup transaction with the default quantity.
            SpawnParameters.CustomPreSpawnInitialization = [&bQuantityInitialized, Quantity = DropItem.DropQuantity,
                Origin = Start + FVector::UpVector * 20.f, Index](AActor* Actor)
            {
                auto* Pickup = CastChecked<ARPGPickUpBase>(Actor);
                bQuantityInitialized = Pickup->InitializeServerDropQuantity(Quantity);
                if (bQuantityInitialized) Pickup->InitializeDropPresentation(Origin, Index);
                if (!bQuantityInitialized) Pickup->TryClaimPickup();
            };
            ARPGPickUpBase* SpawnedItem = World->SpawnActor<ARPGPickUpBase>(DropItem.ItemClass, Landing, FRotator::ZeroRotator, SpawnParameters);
            if (IsValid(SpawnedItem))
            {
                if (!bQuantityInitialized)
                {
                    SpawnedItem->Destroy();
                    UE_LOG(LogTemp, Warning, TEXT("NPC drop rejected after construction: invalid pickup quantity/manifest"));
                    continue;
                }
                UE_LOG(LogTemp, Log, TEXT("SpawnItem: Dropped item %s [x%d] at location %s"), *DropItem.ItemClass->GetName(), DropItem.DropQuantity, *Landing.ToString());
            }
        }
    }
}
