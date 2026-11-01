#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Manager/RPGManagerContext.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "PoolManager.generated.h"

class Pool
{
public:
    Pool(TSubclassOf<AActor> InActorClass, int32 InitialSize = 10, UWorld* InWorld = nullptr);
    void WarmUp(int32 InitialSize);
    AActor* Acquire();
    UClass* GetActorClass() const { return ActorClass.Get(); }
    bool Release(AActor* Actor);
    void Reset();

private:
    AActor* SpawnActor();
    TStrongObjectPtr<UClass> ActorClass;
    TArray<TWeakObjectPtr<AActor>> ActorPool;
    TArray<TWeakObjectPtr<AActor>> AvailableActors;
    TWeakObjectPtr<UWorld> World;
};

/** Actor pools belong to the world that owns their actors. */
UCLASS()
class PROJECT_RPG_API UPoolManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Game-thread access to this context's world. Missing owners use a stateless receiver. */
    static UPoolManager* Get(const UObject* Context);

    /** Compatibility for existing Get<UPoolManager>(Context) callers. */
    template<typename T>
    static T* Get(UObject* Context)
    {
        if constexpr (std::is_same_v<T, UPoolManager>)
        {
            return Get(static_cast<const UObject*>(Context));
        }
        else
        {
            return UWorld::GetSubsystem<T>(RPGManagerContext::ResolveWorld(Context));
        }
    }

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void RegisterPool(FName PoolName, TSubclassOf<AActor> ActorClass, int32 InitialSize = 10);
    void UnregisterPool(FName PoolName);
    AActor* AcquireFromPool(FName PoolName);
    void ReleaseToPool(FName PoolName, AActor* Actor);
    bool TryReleaseToPool(FName PoolName, AActor* Actor);

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    TMap<FName, TSharedPtr<Pool>> PoolMap;
    bool bDeinitializing = true;

    bool CanProcessRequests() const;
};
