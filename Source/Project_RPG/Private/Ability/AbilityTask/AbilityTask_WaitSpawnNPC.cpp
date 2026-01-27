// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/AbilityTask/AbilityTask_WaitSpawnNPC.h"
#include "AbilitySystemComponent.h"
#include "Engine/AssetManager.h"
#include "NavigationSystem.h"
#include "Character/RPGNonPlayerCharacter.h"

#include "RPGDebugHelper.h"

UAbilityTask_WaitSpawnNPC* UAbilityTask_WaitSpawnNPC::WaitSpawnNPCs(UGameplayAbility* OwningAbility, 
	FGameplayTag EventTag, TSoftClassPtr<ARPGNonPlayerCharacter> SoftNPCClassToSpawn,
	int32 NumToSpawn, const FVector& SpawnOrigin, float RandomSpawnRadius)
{
    UAbilityTask_WaitSpawnNPC* Node = NewAbilityTask<UAbilityTask_WaitSpawnNPC>(OwningAbility);
    Node->CachedEventTag = EventTag;
    Node->CachedSoftNPCClassToSpawn = SoftNPCClassToSpawn;
    Node->CachedNumToSpawn = NumToSpawn;
    Node->CachedSpawnOrigin = SpawnOrigin;
    Node->CachedRandomSpawnRadius = RandomSpawnRadius;

    return Node;
}

void UAbilityTask_WaitSpawnNPC::Activate()
{
    FGameplayEventMulticastDelegate& Delegate = AbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(CachedEventTag);

    DelegateHandle = Delegate.AddUObject(this, &ThisClass::OnGameplayEventReceived);
}

void UAbilityTask_WaitSpawnNPC::OnDestroy(bool bInOwnerFinished)
{
    FGameplayEventMulticastDelegate& Delegate = AbilitySystemComponent->GenericGameplayEventCallbacks.FindOrAdd(CachedEventTag);

    Delegate.Remove(DelegateHandle);

    Super::OnDestroy(bInOwnerFinished);
}

void UAbilityTask_WaitSpawnNPC::OnGameplayEventReceived(const FGameplayEventData* InPayload)
{
    if (ensure(!CachedSoftNPCClassToSpawn.IsNull()))
    {
        UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
            CachedSoftNPCClassToSpawn.ToSoftObjectPath(),
            FStreamableDelegate::CreateUObject(this, &ThisClass::OnNPCClassLoaded)
        );
    }
    else
    {
        if (ShouldBroadcastAbilityTaskDelegates())
        {
            DidNotSpawn.Broadcast(TArray<ARPGNonPlayerCharacter*>());
        }

        EndTask();
    }
}

void UAbilityTask_WaitSpawnNPC::OnNPCClassLoaded()
{
    UClass* LoadedClass = CachedSoftNPCClassToSpawn.Get();
    UWorld* World = GetWorld();

    if (!LoadedClass || !World)
    {
        if (ShouldBroadcastAbilityTaskDelegates())
        {
            DidNotSpawn.Broadcast(TArray<ARPGNonPlayerCharacter*>());
        }

        EndTask();

        return;
    }

    TArray<ARPGNonPlayerCharacter*> SpawnedEnemies;

    FActorSpawnParameters SpawnParam;
    SpawnParam.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    for (int32 i = 0; i < CachedNumToSpawn; i++)
    {
        FVector RandomLocation;
        UNavigationSystemV1::K2_GetRandomReachablePointInRadius(this, CachedSpawnOrigin, RandomLocation, CachedRandomSpawnRadius);

        RandomLocation += FVector(0.f, 0.f, 150.f);

        const FRotator SpawnFacingRotation = 
            AbilitySystemComponent->GetAvatarActor()->GetActorForwardVector().ToOrientationRotator();

        ARPGNonPlayerCharacter* SpawnedNPC =
            World->SpawnActor<ARPGNonPlayerCharacter>(LoadedClass, RandomLocation, SpawnFacingRotation, SpawnParam);

        if (SpawnedNPC)
        {
            SpawnedEnemies.Add(SpawnedNPC);
        }
    }

    if (ShouldBroadcastAbilityTaskDelegates())
    {
        if (!SpawnedEnemies.IsEmpty())
        {
            OnSpawnFinished.Broadcast(SpawnedEnemies);
        }
        else
        {
            DidNotSpawn.Broadcast(TArray<ARPGNonPlayerCharacter*>());
        }
    }

    EndTask();
}
