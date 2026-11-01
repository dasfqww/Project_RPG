#include "GameMode/RPGGameModeBase.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

bool ARPGGameModeBase::GetDungeonMembershipPresentation(const FString& ExpectedSessionId,
	FRPGDungeonMembershipPresentation& Out) const
{
	Out = {};
	const UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown || World->GetAuthGameMode() != this
		|| ExpectedSessionId.IsEmpty() || DungeonMembershipMonitor.GetSessionId() != ExpectedSessionId
		|| DungeonMembershipPresentation.Revision == 0
		|| DungeonMembershipPresentation.Snapshot.DungeonSessionId != ExpectedSessionId
		|| DungeonMembershipPresentation.Snapshot.ServerId != DungeonMembershipMonitor.GetServerId()) return false;
	Out = DungeonMembershipPresentation;
	const double Age = FPlatformTime::Seconds() - Out.ReceivedAtSeconds;
	Out.bStale = !FMath::IsFinite(Age) || Age < 0.0 || Age > FMath::Max(10.0, 2.0 * DungeonMembershipPollIntervalSeconds);
	return true;
}

bool ARPGGameModeBase::IsDungeonPartyMemberRevoked(const FString& SessionId,
	const FString& CharacterId, const FString& SteamId) const
{
	const UWorld* World = GetWorld();
	return HasAuthority() && IsValid(World) && !World->bIsTearingDown && World->GetAuthGameMode() == this
		&& !SessionId.IsEmpty() && DungeonMembershipMonitor.GetSessionId() == SessionId
		&& !GetDungeonAdmissionRevocationReason(SessionId, CharacterId, SteamId).IsEmpty();
}
