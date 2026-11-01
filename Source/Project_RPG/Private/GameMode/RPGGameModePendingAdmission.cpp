#include "GameMode/RPGGameModeBase.h"

#include "Engine/ChildConnection.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Player/RPGPlayerState.h"

bool ARPGGameModeBase::RequestPendingDungeonAdmissionEjection(APlayerController* Controller)
{
	if (!IsInGameThread()) return false;
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this || !IsValid(Controller)
		|| Controller->GetWorld() != World || !Controller->HasAuthority()) return false;

	const TWeakObjectPtr<APlayerController> Key(Controller);
	const FAdmissionAwaitingBackend* Pending = AdmissionsAwaitingBackend.Find(Key);
	if (!Pending || ActivatedAdmissions.Contains(Key)) return false;
	const FValidatedAdmission Expected = Pending->Admission;
	ARPGPlayerState* State = Pending->PlayerState.Get();
	UNetConnection* Connection = Pending->Connection.Get();
	UNetDriver* Driver = IsValid(Connection) ? Connection->GetDriver() : nullptr;
	// Pending ticket identity is server-owned. Never infer a departure from an
	// unauthenticated/missing PlayerState, failed read, local clock or old socket.
	if (Expected.ConnectionGeneration <= 0 || !IsValid(State) || !State->HasAuthority()
		|| State->GetWorld() != World || State->GetOwner() != Controller
		|| State != Controller->GetPlayerState<ARPGPlayerState>() || State->HasAuthenticatedCharacter()
		|| State->IsAuthenticatedIdentityRevoked() || State->IsInactive() || Controller->GetPawn()
		|| !IsValid(Connection) || Controller->Player != Connection || Controller->GetNetConnection() != Connection
		|| Connection->PlayerController != Controller || Connection->OwningActor != Controller
		|| Connection->GetConnectionState() != USOCK_Open || Connection->IsReplay()
		|| Cast<UChildConnection>(Connection) || !Connection->Children.IsEmpty()
		|| !IsValid(Driver) || Driver != World->GetNetDriver() || Driver->GetWorld() != World
		|| Driver->ServerConnection || !Driver->ClientConnections.Contains(Connection)) return false;
	const FString Reason = GetDungeonAdmissionRevocationReason(
		Expected.DungeonSessionId, Expected.CharacterId, Expected.SteamId);
	if (Reason.IsEmpty()) return false;

	// Invalidate the exact waiter before engine Close/CleanUp or a late 200 can
	// re-enter. Nothing was activated: do not revoke another identity, spawn or
	// destroy a Pawn, report unexpected loss, or manually perform Logout.
	AdmissionsAwaitingBackend.Remove(Key);
	Connection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended, Reason));
	return Connection->GetConnectionState() == USOCK_Closed;
}
