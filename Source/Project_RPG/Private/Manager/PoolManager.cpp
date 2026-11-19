#include "Manager/PoolManager.h"

#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGPoolManager, Log, All);

Pool::Pool(TSubclassOf<AActor> InActorClass, int32 InitialSize, UWorld* InWorld)
    : ActorClass(InActorClass.Get()), World(InWorld)
{
    WarmUp(InitialSize);
}

void Pool::WarmUp(int32 InitialSize)
{
    for (int32 Index = 0; Index < FMath::Max(0, InitialSize); ++Index)
    {
        AActor* Actor = SpawnActor();
        if (!Actor) { break; }
        Actor->SetActorHiddenInGame(true);
        Actor->SetActorEnableCollision(false);
        AvailableActors.Add(Actor);
    }
}

AActor* Pool::SpawnActor()
{
    UWorld* OwnerWorld = World.Get();
    if (!OwnerWorld || OwnerWorld->bIsTearingDown || !ActorClass) { return nullptr; }
    AActor* Actor = OwnerWorld->SpawnActor<AActor>(ActorClass.Get());
    // Spawn/BeginPlay can unregister the pool or start world teardown.
    if (World.Get() != OwnerWorld || OwnerWorld->bIsTearingDown)
    {
        if (IsValid(Actor)) { Actor->Destroy(); }
        return nullptr;
    }
    return TrackActor(Actor) ? Actor : nullptr;
}

AActor* Pool::Acquire()
{
    if (AActor* Actor = TakeAvailableActor())
    {
        Actor->SetActorHiddenInGame(false);
        Actor->SetActorEnableCollision(true);
        return Actor;
    }
    return SpawnActor();
}

AActor* Pool::TakeAvailableActor()
{
    UWorld* OwnerWorld = World.Get();
    if (!OwnerWorld || OwnerWorld->bIsTearingDown) { return nullptr; }
    ActorPool.RemoveAll([](const TWeakObjectPtr<AActor>& Actor) { return !Actor.IsValid(); });
    while (!AvailableActors.IsEmpty())
    {
        AActor* Actor = AvailableActors.Pop().Get();
        if (!IsValid(Actor) || Actor->GetWorld() != OwnerWorld) { continue; }
        return Actor;
    }
    return nullptr;
}

bool Pool::TrackActor(AActor* Actor)
{
    if (!IsValid(Actor) || Actor->GetWorld() != World.Get() || Actor->GetClass() != ActorClass.Get())
    {
        return false;
    }
    ActorPool.AddUnique(Actor);
    return true;
}

bool Pool::Release(AActor* Actor, int32 MaxAvailableActors)
{
    const TWeakObjectPtr<AActor> Reference(Actor);
    if (!IsValid(Actor) || Actor->GetWorld() != World.Get() || !ActorPool.Contains(Reference))
    {
        return false;
    }
    if (!AvailableActors.Contains(Reference))
    {
        if (AvailableActors.Num() >= MaxAvailableActors)
        {
            ActorPool.RemoveSingleSwap(Reference);
            return false;
        }
        Actor->SetActorHiddenInGame(true);
        Actor->SetActorEnableCollision(false);
        Actor->SetActorLocation(FVector::ZeroVector);
        AvailableActors.Add(Reference);
    }
    return true;
}

void Pool::Reset()
{
    TArray<TWeakObjectPtr<AActor>> Removed = MoveTemp(ActorPool);
    AvailableActors.Reset();
    UWorld* OwnerWorld = World.Get();
    World.Reset();
    // World teardown already owns actor destruction.
    if (!OwnerWorld || OwnerWorld->bIsTearingDown) { return; }
    for (const TWeakObjectPtr<AActor>& Reference : Removed)
    {
        if (AActor* Actor = Reference.Get()) { Actor->Destroy(); }
    }
}

UPoolManager* UPoolManager::Get(const UObject* Context)
{
    check(IsInGameThread());
    UWorld* World = RPGManagerContext::ResolveWorld(Context);
    UPoolManager* Manager = World ? World->GetSubsystem<UPoolManager>() : nullptr;
    return IsValid(Manager) && !Manager->bDeinitializing
        ? Manager : GetMutableDefault<UPoolManager>();
}

bool UPoolManager::CanProcessRequests() const
{
    if (!IsInGameThread() || HasAnyFlags(RF_ClassDefaultObject) || bDeinitializing) { return false; }
    UWorld* World = GetWorld();
    return IsValid(World) && !World->bIsTearingDown;
}

bool UPoolManager::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE
        || WorldType == EWorldType::GamePreview;
}

void UPoolManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bDeinitializing = false;
}

void UPoolManager::Deinitialize()
{
    bDeinitializing = true;
    TMap<FName, TSharedPtr<Pool>> Removed = MoveTemp(PoolMap);
    for (auto& Pair : Removed) { Pair.Value->Reset(); }
    Super::Deinitialize();
}

void UPoolManager::RegisterPool(FName PoolName, TSubclassOf<AActor> ActorClass, int32 InitialSize)
{
    if (!CanProcessRequests()) { return; }
    UWorld* World = GetWorld();
    if (PoolName.IsNone() || !ActorClass
        || ActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) { return; }
    if (PoolMap.Contains(PoolName))
    {
        if (PoolMap.FindRef(PoolName)->GetActorClass() != ActorClass.Get())
        {
            UE_LOG(LogRPGPoolManager, Warning, TEXT("Pool '%s' already uses another actor class."),
                *PoolName.ToString());
        }
        return;
    }
    TSharedPtr<Pool> Registered = MakeShared<Pool>(ActorClass, 0, World);
    PoolMap.Add(PoolName, Registered);
    Registered->WarmUp(InitialSize);
}

void UPoolManager::UnregisterPool(FName PoolName)
{
    if (!CanProcessRequests()) { return; }
    TSharedPtr<Pool> Removed;
    if (PoolMap.RemoveAndCopyValue(PoolName, Removed))
    {
        // Remove the registration before destroying actors, which can re-enter gameplay.
        Removed->Reset();
    }
}

AActor* UPoolManager::AcquireFromPool(FName PoolName)
{
    if (!CanProcessRequests()) { return nullptr; }
    TSharedPtr<Pool> Found = PoolMap.FindRef(PoolName);
    return Found ? Found->Acquire() : nullptr;
}

AActor* UPoolManager::AcquireInactiveFromPool(FName PoolName)
{
    if (!CanProcessRequests()) { return nullptr; }
    TSharedPtr<Pool> Found = PoolMap.FindRef(PoolName);
    return Found ? Found->TakeAvailableActor() : nullptr;
}

bool UPoolManager::TrackActorForPool(FName PoolName, AActor* Actor)
{
    if (!CanProcessRequests()) { return false; }
    TSharedPtr<Pool> Found = PoolMap.FindRef(PoolName);
    return Found && Found->TrackActor(Actor);
}

void UPoolManager::ReleaseToPool(FName PoolName, AActor* Actor)
{
    TryReleaseToPool(PoolName, Actor);
}

bool UPoolManager::TryReleaseToPool(FName PoolName, AActor* Actor, int32 MaxAvailableActors)
{
    if (!CanProcessRequests()) { return false; }
    TSharedPtr<Pool> Found = PoolMap.FindRef(PoolName);
    return Found && Found->Release(Actor, MaxAvailableActors);
}
