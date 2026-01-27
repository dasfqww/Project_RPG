// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/NPC/RPGNPCDropItemAbility.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Item/RPGItemBase.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Type/RPGStructTypes.h"
#include "DataTable/DropItemData.h"

#include "RPGDebugHelper.h"

void URPGNPCDropItemAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, 
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, 
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    SpawnItem();
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
    AActor* NPC = GetNonPlayerCharacterFromActorInfo();
    if (!NPC)
    {
        UE_LOG(LogTemp, Warning, TEXT("SpawnItem: NPC is nullptr"));
        return;
    }

    FVector Start = NPC->GetActorLocation();
    FVector End = Start + (NPC->GetActorUpVector() * -200.0f);

    TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
    ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));

    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(NPC);

    FHitResult HitResult;
    bool bHit = UKismetSystemLibrary::LineTraceSingleForObjects(
        GetWorld(),
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

    const FItemDropTable* DropData = ItemDropTable->FindRow<FItemDropTable>(DropRowName, TEXT("ItemList"));

    if (!ItemDropTable)
    {
        UE_LOG(LogTemp, Warning, TEXT("SpawnItem: DropTable is nullptr"));
        return;
    }

    for (const FDropItem& DropItem : DropData->DropItemList)
    {
        if (FMath::FRandRange(0.0f, 1.0f) <= DropItem.DropChance) // Ȯ�� üũ
        {
            if (!DropItem.ItemClass) // ����� ������ Ŭ������ ��ȿ���� Ȯ��
            {
                UE_LOG(LogTemp, Warning, TEXT("SpawnItem: ItemClass is nullptr"));
                continue;
            }

            // ��� ������ ����
            ARPGPickUpBase* SpawnedItem = GetWorld()->SpawnActor<ARPGPickUpBase>(DropItem.ItemClass, DropLocation, FRotator::ZeroRotator);
            if (SpawnedItem)
            {
                SpawnedItem->InitializeDrop(SpawnedItem->GetRPGItemData(), DropItem.DropQuantity);
                UE_LOG(LogTemp, Log, TEXT("SpawnItem: Dropped item %s [x%d] at location %s"), *DropItem.ItemClass->GetName(), DropItem.DropQuantity, *DropLocation.ToString());
            }
        }
    }
}
