#pragma once
#include "CoreMinimal.h"

/** Local workbench files carry commands/observations, never authoritative combat state. */
namespace RPGSoloRaidControl
{
    PROJECT_RPG_API bool IsMode(const FString& Mode);
    PROJECT_RPG_API bool IsPolicy(const FString& Policy);
    PROJECT_RPG_API bool ReadCommand(const FString& Path, FString& Mode, FString& Identity);
    PROJECT_RPG_API bool WriteCommand(const FString& Path, const FString& Mode);
    PROJECT_RPG_API FString ReadPolicy(const FString& Directory, int32 Slot);
    PROJECT_RPG_API bool WritePolicy(const FString& Directory, int32 Slot, const FString& Policy);
    PROJECT_RPG_API bool WriteAtomic(const FString& Path, const FString& Text);
}
