#include "Tests/RPGDungeonRecoveryTestSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/RPGDungeonSessionSubsystem.h"

bool URPGDungeonRecoveryTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer()
		&& FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonRecoveryTest"));
#else
	return false;
#endif
}

void URPGDungeonRecoveryTestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	Collection.InitializeDependency<URPGDungeonSessionSubsystem>();
	Deadline = FPlatformTime::Seconds() + 600.0;
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickProbe), .1f);
	if (GEngine && FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonPendingRecoveryTest"))
		&& FParse::Param(FCommandLine::Get(), TEXT("nosteam")))
	{
		GEngine->OnNetworkFailure().AddWeakLambda(this,
			[this](UWorld* FailureWorld, UNetDriver* Driver, ENetworkFailure::Type Type, const FString&)
			{
				if (!bSeeded || FailureWorld || !Driver || Driver->NetDriverName != NAME_PendingNetDriver
					|| Type != ENetworkFailure::PendingConnectionFailure || Driver->GetNetMode() != NM_Client) return;
				const FWorldContext* Context = GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver);
				if (Context && Context->OwningGameInstance == GetGameInstance())
					UE_LOG(LogTemp, Display, TEXT("DUNGEON_PENDING_RECOVERY_E2E_FAILURE WorldNull=1 DriverOwned=1 NoBackend=1"));
			});
	}
#endif
}

void URPGDungeonRecoveryTestSubsystem::Deinitialize()
{
	if (GEngine) GEngine->OnNetworkFailure().RemoveAll(this);
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Ticker.Reset();
	Super::Deinitialize();
}

bool URPGDungeonRecoveryTestSubsystem::TickProbe(float)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	if (FPlatformTime::Seconds() > Deadline)
	{
		UE_LOG(LogTemp, Error, TEXT("DUNGEON_RECOVERY_E2E_FAIL Timeout"));
		return false;
	}
	UWorld* World = GetGameInstance()->GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	if (!World || !World->IsGameWorld() || !PC || !PC->IsLocalController() || !Flow) return true;
	const bool bPendingTest = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonPendingRecoveryTest"));
	if (bPendingTest && !FParse::Param(FCommandLine::Get(), TEXT("nosteam"))) return false;
	if (!bSeeded && ((bPendingTest && World->GetNetMode() == NM_Standalone)
		|| (!bPendingTest && World->GetNetMode() == NM_Client)))
	{
		// The fixture is deliberately unauthenticated and never contacts a backend.
		// Exercise real UE failure routing and map loading, not admission/settlement.
		FRPGDungeonSession Session;
		Session.DungeonSessionId = TEXT("offline-recovery-session");
		Session.State = TEXT("InProgress");
		Session.ServerId = TEXT("offline-recovery-server");
		FRPGDungeonSessionMember Member;
		Member.CharacterId = TEXT("offline-recovery-character");
		Session.Members.Add(Member);
		Flow->SelectedCharacterId = Member.CharacterId;
		Flow->CurrentDungeonSession = Session;
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::None;
		Flow->FlowState = bPendingTest ? ERPGDungeonClientFlowState::Connecting : ERPGDungeonClientFlowState::InSession;
		bSeeded = true;
		if (bPendingTest)
		{
			int32 Port = 0;
			if (!FParse::Value(FCommandLine::Get(), TEXT("RPGPendingRecoveryPort="), Port) || Port < 1024 || Port > 65535)
			{
				UE_LOG(LogTemp, Error, TEXT("DUNGEON_RECOVERY_E2E_FAIL InvalidPendingPort"));
					return false;
			}
			// Deliberately omit the ticket: exercise real PreLogin rejection and
			// engine PendingNetGame failure, never fake authentication or Ready.
			PC->ClientTravel(FString::Printf(TEXT("127.0.0.1:%d"), Port), TRAVEL_Absolute);
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_PENDING_RECOVERY_E2E_START OfflineFixture=1 NoBackend=1"));
			return true;
		}
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECOVERY_E2E_CONNECTED OfflineFixture=1 NoBackend=1"));
		return true;
	}
	if (!bSeeded || World->GetNetMode() != NM_Standalone
		|| World->GetOutermost()->GetName() != TEXT("/Game/Maps/LobbyMap")) return true;
	const FRPGDungeonSession Session = Flow->GetCurrentDungeonSession();
	const bool bRetained = Session.DungeonSessionId == TEXT("offline-recovery-session")
		&& Session.State == TEXT("InProgress") && Session.ServerId == TEXT("offline-recovery-server")
		&& Session.Members.Num() == 1 && Session.Members[0].CharacterId == TEXT("offline-recovery-character")
		&& Flow->GetSelectedCharacterId() == TEXT("offline-recovery-character") && Flow->HasActiveDungeonSession();
	const bool bLocalURL = World->URL.Host.IsEmpty() && !World->URL.HasOption(TEXT("JoinTicket"));
	if (!bRetained || !bLocalURL || Flow->IsRequestInFlight()
		|| Flow->GetFlowState() != ERPGDungeonClientFlowState::Idle
		|| Flow->GetLastErrorCode() != TEXT("dungeon_connection_lost"))
	{
		UE_LOG(LogTemp, Error, TEXT("DUNGEON_RECOVERY_E2E_FAIL Retained=%d LocalURL=%d"), bRetained, bLocalURL);
		return false;
	}
	if (bPendingTest)
	{
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_PENDING_RECOVERY_E2E_COMPLETE LobbyLoaded=1 StateRetained=1 MembershipRetained=1 CharacterRetained=1 JoinOptionsRemoved=1 InputGuardReleased=1 NoBackend=1"));
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RECOVERY_E2E_COMPLETE LobbyLoaded=1 StateRetained=1 MembershipRetained=1 CharacterRetained=1 JoinOptionsRemoved=1 InputGuardReleased=1 NoBackend=1"));
	}
#endif
	return false;
}
