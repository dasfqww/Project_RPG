#include "Tests/RPGDungeonFlowE2EProbe.h"

#if !UE_BUILD_SHIPPING
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameMode/RPGGameModeBase.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RPGPlayerState.h"

void URPGDungeonFlowE2EServer::ObservePendingAdmissionAbandonment()
{
	// No reflected/layout changes, fake identity, receipt injection, manual
	// membership read, Close/Logout, or admission completion. Observe production.
	struct FObservation
	{
		int32 Phase = 0;
		FPartyPeer Pending;
		FPartyPeer Survivor;
		TWeakPtr<ARPGGameModeBase::FAdmissionRequestState> Request;
		float SurvivorHealth = 0.f;
		double StableSince = 0.0;
	};
	static TMap<TWeakObjectPtr<URPGDungeonFlowE2EServer>, FObservation> Observations;
	for (auto It = Observations.CreateIterator(); It; ++It)
		if (!It.Key().IsValid()) It.RemoveCurrent();
	FObservation& Observation = Observations.FindOrAdd(this);
	auto* Mode = Cast<ARPGGameModeBase>(GetOwner());
	UWorld* World = GetWorld();
	const auto* Web = World ? World->GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	const auto* Driver = World ? World->GetNetDriver() : nullptr;
	FGuid Target;
	if (!Mode || !World || World->GetAuthGameMode() != Mode || !Mode->HasAuthority()
		|| World->GetNetMode() != NM_DedicatedServer || !Driver || !Driver->IsUsingIrisReplication()
		|| !Web || !Web->IsConfiguredForDungeonServer() || ExpectedPlayers != 2
		|| !FParse::Param(FCommandLine::Get(), TEXT("nosteam"))
		|| !FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonMembershipBackendE2E"))
		|| bAbandonmentSettlement || bResultAbandonment || bNaturalPlayerAttack || bLiveBossAI
		|| FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryConsumeRecoveryServerE2E"))
		|| !FGuid::ParseExact(AbandonCharacter, EGuidFormats::DigitsWithHyphens, Target) || !Target.IsValid()
		|| GatePath.IsEmpty() || IFileManager::Get().FileExists(*GatePath))
	{ Fail(TEXT("PendingAdmissionUnsafeFixture")); return; }

	if (Observation.Phase == 0)
	{
		for (const auto& Pair : Mode->AdmissionsAwaitingBackend)
		{
			const auto& Pending = Pair.Value;
			if (Pending.Admission.CharacterId != AbandonCharacter) continue;
			APlayerController* PC = Pair.Key.Get();
			ARPGPlayerState* State = Pending.PlayerState.Get();
			UNetConnection* Connection = Pending.Connection.Get();
			if (!PC || !State || !Connection || !Pending.RequestState) continue;
			if (Pending.Admission.DungeonSessionId != Web->GetConfiguredDungeonSessionId()
				|| Pending.Admission.ConnectionGeneration != 1 || Pending.Admission.SteamId.IsEmpty()
				|| PC->GetPawn() || State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()
				|| State->GetOwner() != PC || PC->GetPlayerState<ARPGPlayerState>() != State
				|| PC->Player != Connection || PC->GetNetConnection() != Connection
				|| Connection->PlayerController != PC || Connection->OwningActor != PC
				|| Connection->GetConnectionState() != USOCK_Open || Connection->GetDriver() != Driver
				|| !Driver->ClientConnections.Contains(Connection) || Mode->ActivatedAdmissions.Contains(Pair.Key)
				|| !Mode->GetDungeonAdmissionRevocationReason(Pending.Admission.DungeonSessionId,
					Pending.Admission.CharacterId, Pending.Admission.SteamId).IsEmpty())
			{ Fail(TEXT("PendingAdmissionInitialScope")); return; }
			Observation.Pending = { PC, nullptr, State, Connection, Pending.Admission.CharacterId,
				Pending.Admission.SteamId, Pending.Admission.ConnectionGeneration };
			Observation.Request = Pending.RequestState;
		}
		if (!Observation.Pending.Controller.IsValid()) return;
		if (Mode->ActivatedAdmissions.Num() != 1) return;
		for (const auto& Pair : Mode->ActivatedAdmissions)
		{
			APlayerController* PC = Pair.Key.Get();
			auto* Player = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
			auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
			auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
			if (!Player || !State || !ASC || !State->HasAuthenticatedCharacter()) return;
			if (State->GetBackendCharacterId() == AbandonCharacter || State->GetBackendConnectionGeneration() != 1)
			{ Fail(TEXT("PendingAdmissionSurvivorScope")); return; }
			Observation.Survivor = { PC, Player, State, PC->GetNetConnection(), State->GetBackendCharacterId(),
				State->GetAuthenticatedSteamId(), State->GetBackendConnectionGeneration() };
			Observation.SurvivorHealth = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
			if (!FMath::IsFinite(Observation.SurvivorHealth) || Observation.SurvivorHealth <= 0.f) return;
		}
		Observation.Phase = 1;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_PENDING_ADMISSION HELD Iris=1 ConsumedGeneration=1 Pending=1 Authenticated=0 Pawn=0 Activated=0 Survivor=1"));
	}

	const FPartyPeer& Peer = Observation.Survivor;
	APlayerController* Survivor = Peer.Controller.Get();
	auto* Player = Cast<ARPGPlayer>(Peer.Pawn.Get());
	const auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	const auto* SurvivorState = Peer.State.Get();
	const auto* Battle = World->GetGameState<ARPGBossBattleGameState>();
	if (!Survivor || !Player || !ASC || !SurvivorState || Survivor->GetPawn() != Player
		|| Survivor->GetPlayerState<ARPGPlayerState>() != SurvivorState || !SurvivorState->HasAuthenticatedCharacter()
		|| SurvivorState->GetBackendCharacterId() != Peer.CharacterId || SurvivorState->GetAuthenticatedSteamId() != Peer.SteamId
		|| SurvivorState->GetBackendDungeonSessionId() != Web->GetConfiguredDungeonSessionId()
		|| SurvivorState->GetBackendConnectionGeneration() != Peer.ConnectionGeneration
		|| Survivor->GetNetConnection() != Peer.Connection.Get() || !Peer.Connection.IsValid()
		|| Peer.Connection->GetConnectionState() != USOCK_Open || !Mode->ActivatedAdmissions.Contains(Peer.Controller)
		|| ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != Observation.SurvivorHealth
		|| !Battle || Battle->GetBattleSnapshot().BattleState != EBossBattleState::InProgress)
	{ Fail(TEXT("PendingAdmissionSurvivorChanged")); return; }
	const FPartyPeer& Pending = Observation.Pending;
	APlayerController* PC = Pending.Controller.Get();
	const auto* State = Pending.State.Get();
	if ((PC && PC->GetPawn()) || (State && (State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()))
		|| Mode->ActivatedAdmissions.Contains(Pending.Controller))
	{ Fail(TEXT("PendingAdmissionAuthorityPublished")); return; }
	const bool bReceipt = !Mode->GetDungeonAdmissionRevocationReason(Web->GetConfiguredDungeonSessionId(),
		Pending.CharacterId, Pending.SteamId).IsEmpty();
	const bool bWaiter = Mode->AdmissionsAwaitingBackend.Contains(Pending.Controller);
	UNetConnection* Connection = Pending.Connection.Get();
	const bool bClosed = !Connection || Connection->GetConnectionState() == USOCK_Closed;
	if (Observation.Phase == 1)
	{
		if (!bReceipt)
		{
			if (bClosed || !bWaiter || !Observation.Request.IsValid()) Fail(TEXT("PendingAdmissionClosedBeforeReceipt"));
			return;
		}
		if (bWaiter || !bClosed) { Fail(TEXT("PendingAdmissionReceiptDidNotClose")); return; }
		if (!Observation.Request.IsValid()) { Fail(TEXT("PendingAdmissionReplyNotHeldAtClose")); return; }
		Observation.Phase = 2;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_PENDING_ADMISSION CLOSED ImmutableReceipt=1 Waiter=0 SocketClosed=1 ReplyStillPending=1 NoIdentity=1 NoPawn=1 SurvivorUnchanged=1"));
	}
	if (Observation.Phase == 2)
	{
		if (!bReceipt || bWaiter || !bClosed) { Fail(TEXT("PendingAdmissionRevocationRegressed")); return; }
		const FString ReleaseGate = FPaths::Combine(FPaths::GetPath(GatePath), TEXT("release-confirmation.gate"));
		if (!IFileManager::Get().FileExists(*ReleaseGate))
		{
			if (!Observation.Request.IsValid()) Fail(TEXT("PendingAdmissionReplyExpiredBeforeRelease"));
			return;
		}
		// The callback is the sole remaining owner after waiter removal. Expiry
		// observes its disposal; it does not call or replace the callback itself.
		if (Observation.Request.IsValid()) return;
		if (Observation.StableSince == 0.0) Observation.StableSince = FPlatformTime::Seconds();
		if (FPlatformTime::Seconds() - Observation.StableSince < 2.0) return;
		Observation.Phase = 3;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_PENDING_ADMISSION LATE_REPLY_INERT CallbackReleased=1 Waiter=0 Activated=0 NoIdentity=1 NoPawn=1 SurvivorUnchanged=1"));
		SetComponentTickEnabled(false);
		Observations.Remove(this);
	}
}
#endif
