// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/NPC/RPGNPCDropItemAbility.h"
#include "Item/Loot/RPGWorldLoot.h"
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

    TArray<FDropItem> Drops;
    if (!RPGWorldLoot::ReadDrops(ItemDropTable, DropRowName, Drops))
    {
        UE_LOG(LogTemp, Warning, TEXT("NPC drop rejected: invalid table, row, probability, pickup or quantity"));
        return;
    }
    if (NPC->GetActorLocation().ContainsNaN() || !NPC->TryBeginDeathItemDrop()) return;
    RPGWorldLoot::Spawn(*NPC, Drops);
}
