#pragma once
#include "CoreMinimal.h"
#include "Type/RPGStructTypes.h"
class APlayerController;
class ARPGPlayerState;
class UDataTable;

/** Shared server spawn path. Callers must consume their event before Spawn invokes user callbacks. */
namespace RPGWorldLoot
{
    PROJECT_RPG_API bool ReadDrops(const UDataTable* Table, FName Row, TArray<FDropItem>& Out);
    PROJECT_RPG_API bool Validate(const TArray<FDropItem>& Drops);
    PROJECT_RPG_API int32 Spawn(AActor& Source, const TArray<FDropItem>& FrozenDrops);
    PROJECT_RPG_API ARPGPlayerState* Collector(const AActor& Target, APlayerController* Controller, float Distance = 250.f);
}
