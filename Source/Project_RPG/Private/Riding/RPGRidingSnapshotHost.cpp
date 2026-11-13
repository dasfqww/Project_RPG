#include "Riding/RPGRidingSnapshot.h"
#include "Manager/HttpWebManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IRPGDungeonRecoveryHost* RPGRidingSnapshot::ResolveHost(UWorld* World)
{
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || World->GetNetMode() == NM_Client) return nullptr;
    auto* Instance = World->GetGameInstance();
    return Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
}
