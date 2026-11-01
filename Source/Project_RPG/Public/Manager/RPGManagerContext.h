#pragma once

#include "CoreMinimal.h"

class UGameInstance;
class UWorld;

/** Resolve existing engine owners. These helpers never create or retain a manager. */
namespace RPGManagerContext
{
    PROJECT_RPG_API UWorld* ResolveWorld(const UObject* Context);
    /** A null context is a compatibility path for existing context-free callers. */
    PROJECT_RPG_API UGameInstance* ResolveGameInstance(const UObject* Context);
}
