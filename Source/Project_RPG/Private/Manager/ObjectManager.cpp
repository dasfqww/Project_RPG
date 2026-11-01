#include "Manager/ObjectManager.h"

#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "Manager/PoolManager.h"

namespace
{
    int32 CountValidActors(const TSet<TWeakObjectPtr<AActor>>& Actors)
    {
        int32 Count = 0;
        for (const TWeakObjectPtr<AActor>& Actor : Actors) { Count += Actor.IsValid() ? 1 : 0; }
        return Count;
    }
}

UObjectManager* UObjectManager::Get(const UObject* Context)
{
    check(IsInGameThread());
    UWorld* World = RPGManagerContext::ResolveWorld(Context);
    UObjectManager* Manager = World ? World->GetSubsystem<UObjectManager>() : nullptr;
    return IsValid(Manager) && !Manager->bDeinitializing
        ? Manager : GetMutableDefault<UObjectManager>();
}

bool UObjectManager::CanProcessRequests() const
{
    if (!IsInGameThread() || HasAnyFlags(RF_ClassDefaultObject) || bDeinitializing) { return false; }
    UWorld* World = GetWorld();
    return IsValid(World) && !World->bIsTearingDown;
}

bool UObjectManager::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE
        || WorldType == EWorldType::GamePreview;
}

void UObjectManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UPoolManager>();
    bDeinitializing = false;
}

void UObjectManager::Deinitialize()
{
    bDeinitializing = true;
    for (const TWeakObjectPtr<AActor>& Reference : ManagedObjects)
    {
        if (AActor* Actor = Reference.Get())
        {
            Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::OnManagedActorEndPlay);
        }
    }
    ManagedObjects.Reset();
    Players.Reset();
    Monsters.Reset();
    Super::Deinitialize();
}

AActor* UObjectManager::SpawnObject(FName PoolName, TSubclassOf<AActor> ActorClass,
    const FVector& Location, const FRotator& Rotation, bool bUsePooling)
{
    if (!CanProcessRequests() || !ActorClass) { return nullptr; }
    UWorld* World = GetWorld();

    AActor* Actor = nullptr;
    if (bUsePooling)
    {
        UPoolManager* Pools = UPoolManager::Get(this);
        Pools->RegisterPool(PoolName, ActorClass, 15);
        Actor = Pools->AcquireFromPool(PoolName);
        if (IsValid(Actor) && Actor->GetClass() != ActorClass.Get())
        {
            Pools->TryReleaseToPool(PoolName, Actor);
            return nullptr;
        }
        if (IsValid(Actor))
        {
            Actor->SetActorLocation(Location);
            Actor->SetActorRotation(Rotation);
        }
    }
    else
    {
        Actor = World->SpawnActor<AActor>(ActorClass, Location, Rotation);
    }

    if (!IsValid(Actor)) { return nullptr; }
    ManagedObjects.Add(Actor);
    Actor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnManagedActorEndPlay);
    return Actor;
}

void UObjectManager::Despawn(FName PoolName, AActor* Actor, bool bUsePooling)
{
    if (!CanProcessRequests() || !IsValid(Actor) || Actor->GetWorld() != GetWorld()) { return; }
    Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::OnManagedActorEndPlay);
    OnManagedActorEndPlay(Actor, EEndPlayReason::RemovedFromWorld);

    if (!bUsePooling || !UPoolManager::Get(this)->TryReleaseToPool(PoolName, Actor))
    {
        Actor->Destroy();
    }
}

void UObjectManager::OnManagedActorEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
    ManagedObjects.Remove(Actor);
    Players.Remove(Actor);
    Monsters.Remove(Actor);
}

int32 UObjectManager::GetPlayerCount() const
{
    return CanProcessRequests() ? CountValidActors(Players) : 0;
}
int32 UObjectManager::GetMonsterCount() const
{
    return CanProcessRequests() ? CountValidActors(Monsters) : 0;
}
int32 UObjectManager::GetManagedCount() const
{
    return CanProcessRequests() ? CountValidActors(ManagedObjects) : 0;
}
