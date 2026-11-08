#pragma once
#include "CoreMinimal.h"

struct PROJECT_RPG_API FRPGTradeTarget
{
    FGuid Character;
    FString Name;
};
enum class ERPGTradeTargetState : uint8 { Idle, Loading, Ready, Empty, Failed, Throttled };
namespace RPGTradeTargets
{
PROJECT_RPG_API bool NormalizeName(const FString& Input, FString& Out);
PROJECT_RPG_API bool Decode(const FString& Json, FGuid Actor, const FString& Query, TArray<FRPGTradeTarget>& Out);
}
