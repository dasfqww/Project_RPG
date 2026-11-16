#pragma once
#include "CoreMinimal.h"
#include "AI/RPGBossTelegraphShape.h"

class ARPGBossCharacter;
class ARPGBaseCharacter;
class ARPGPlayer;
class UWorld;

namespace RPGSoloRaidBot
{
    PROJECT_RPG_API float Health(const AActor* Actor);
    PROJECT_RPG_API ARPGBaseCharacter* SelectTarget(ARPGBossCharacter& Boss, ARPGPlayer& Player, bool bIgnoreMechanic);
    PROJECT_RPG_API TArray<FRPGBossTelegraphGeometry> ReadDangers(ARPGBossCharacter& Boss);
    PROJECT_RPG_API bool TouchesDanger(const TArray<FRPGBossTelegraphGeometry>& Dangers, FVector Point, float Clearance);
    PROJECT_RPG_API bool FindEscape(const TArray<FRPGBossTelegraphGeometry>& Dangers, FVector Point, float Clearance, FVector& Goal);
    PROJECT_RPG_API ARPGPlayer* FindLeader(UWorld& World, int32 PlayerId);
}
