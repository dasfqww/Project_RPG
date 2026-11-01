#include "Security/RPGSecurityTelemetrySubsystem.h"

#include "Engine/GameInstance.h"
#include "GameMode/RPGGameModeBase.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonMembershipSnapshot.h"

bool URPGSecurityTelemetrySubsystem::PrepareMembershipDelivery()
{
	UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	const auto* Mode = World ? World->GetAuthGameMode<ARPGGameModeBase>() : nullptr;
	const auto* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
	FRPGDungeonMembershipPresentation Presentation;
	if (!IsInGameThread() || !bAvailable || bShuttingDown || Queue.HasBatchInFlight() || ActiveRequest.IsValid()
		|| !IsValid(World) || World->bIsTearingDown || !IsValid(Mode) || !Mode->HasAuthority()
		|| Mode->GetWorld() != World || Mode->GetGameInstance() != Instance
		|| !Web || !Web->IsConfiguredForDungeonServer()
		|| DungeonSessionId != Web->GetConfiguredDungeonSessionId()
		|| !Mode->GetDungeonMembershipPresentation(DungeonSessionId, Presentation)
		|| Presentation.Snapshot.ServerId != Web->GetGameServerId() || Presentation.bStale)
	{
		// Defer transport without deleting/reserving evidence, inferring a
		// departure, sending another GET, or changing gameplay/admission rights.
		return false;
	}

	// Stage the union first. An account conflict or capacity failure must not
	// partially quarantine a mixed queue or discard another member's event IDs.
	TMap<FString, FString> NextRevocations = RevokedMembers;
	for (const FRPGSecurityTelemetryEvent& Event : Queue.GetEvents())
	{
		// The presentation's age only gates delivery. Revocation authority comes
		// from the existing monotonic immutable-receipt monitor, not member
		// absence, actor presence, a failed GET, or the local clock.
		if (!Mode->IsDungeonPartyMemberRevoked(DungeonSessionId, Event.CharacterId, Event.SteamId)) continue;
		FGuid Character;
		if (!FGuid::ParseExact(Event.CharacterId, EGuidFormats::DigitsWithHyphens, Character) || !Character.IsValid()) return false;
		const FString Canonical = Character.ToString(EGuidFormats::DigitsWithHyphensLower);
		if (const FString* Previous = NextRevocations.Find(Canonical); Previous && *Previous != Event.SteamId) return false;
		NextRevocations.Add(Canonical, Event.SteamId);
		if (NextRevocations.Num() > 128) return false;
	}
	if (NextRevocations.Num() != RevokedMembers.Num())
	{
		RevokedMembers = MoveTemp(NextRevocations);
		bRevocationsDirty = true;
	}
	// Existing ordering: durable tombstones -> dead-letter preservation ->
	// pending queue/spool update. Any preservation failure keeps delivery shut.
	return QuarantineRevokedPendingEvents();
}
