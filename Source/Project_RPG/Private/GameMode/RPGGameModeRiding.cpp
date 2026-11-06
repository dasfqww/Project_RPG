#include "GameMode/RPGGameModeBase.h"
#include "Player/RPGPlayerState.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/ChildConnection.h"
#include "Engine/NetDriver.h"
#include "GameFramework/PlayerController.h"

bool ARPGGameModeBase::HasConfirmedDungeonAdmission(const ARPGPlayerState* State) const
{
    const auto* World = GetWorld();
    const auto* PC = State ? Cast<APlayerController>(State->GetOwner()) : nullptr;
    if (!IsInGameThread() || !HasAuthority() || bDungeonConnectionReportingStopped || !IsValid(World)
        || World->bIsTearingDown || World->GetAuthGameMode() != this || !IsValid(State) || !IsValid(PC)
        || !State->HasAuthority() || State->GetWorld() != World || PC->GetWorld() != World
        || PC->GetPlayerState<ARPGPlayerState>() != State || !State->HasAuthenticatedCharacter()) return false;
    const auto* Active = ActivatedAdmissions.Find(TWeakObjectPtr<APlayerController>(const_cast<APlayerController*>(PC)));
    if (!Active || Active->bPlannedLobbyDisconnect || Active->PlayerState != State) return false;
    const auto* Connection = Active->Connection.Get();
    const auto* Driver = Connection ? Connection->GetDriver() : nullptr;
    const auto& Expected = Active->Admission;
    return IsValid(Connection) && IsValid(Driver) && Driver == World->GetNetDriver() && Driver->GetWorld() == World
        && !Driver->ServerConnection && Driver->ClientConnections.Contains(Connection)
        && PC->Player == Connection && PC->GetNetConnection() == Connection && Connection->PlayerController == PC
        && Connection->OwningActor == PC && Connection->GetConnectionState() == USOCK_Open
        && !Cast<UChildConnection>(Connection) && Connection->Children.IsEmpty() && !Connection->IsReplay()
        && Expected.ConnectionGeneration > 0 && State->GetBackendConnectionGeneration() == Expected.ConnectionGeneration
        && State->GetBackendCharacterId() == Expected.CharacterId && State->GetBackendDungeonSessionId() == Expected.DungeonSessionId
        && State->GetAuthenticatedSteamId() == Expected.SteamId
        && GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId, Expected.CharacterId, Expected.SteamId).IsEmpty();
}
