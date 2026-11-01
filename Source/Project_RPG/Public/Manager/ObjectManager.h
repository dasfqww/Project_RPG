#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Manager/RPGManagerContext.h"
#include "Subsystems/WorldSubsystem.h"
#include "ObjectManager.generated.h"

/** Tracks spawned actors in their owning world without extending actor lifetime. */
UCLASS()
class PROJECT_RPG_API UObjectManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Game-thread access to this context's world. Missing owners use a stateless receiver. */
    static UObjectManager* Get(const UObject* Context);

    /** Compatibility for existing Get<UObjectManager>(Context) callers. */
    template<typename T>
    static T* Get(UObject* Context)
    {
        if constexpr (std::is_same_v<T, UObjectManager>)
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

    AActor* SpawnObject(FName PoolName, TSubclassOf<AActor> ActorClass,
        const FVector& Location, const FRotator& Rotation, bool bUsePooling = true);
    void Despawn(FName PoolName, AActor* Actor, bool bUsePooling = true);

    int32 GetPlayerCount() const;
    int32 GetMonsterCount() const;
    int32 GetManagedCount() const;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    UFUNCTION()
    void OnManagedActorEndPlay(AActor* Actor, EEndPlayReason::Type Reason);

    TSet<TWeakObjectPtr<AActor>> ManagedObjects;
    TSet<TWeakObjectPtr<AActor>> Players;
    TSet<TWeakObjectPtr<AActor>> Monsters;
    bool bDeinitializing = true;

    bool CanProcessRequests() const;
};
