#include "Tests/RPGDungeonEjectionTestFixture.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Controller/RPGPlayerController.h"

void URPGDungeonMutationTestGameInstance::SetTestWorldContext(FWorldContext* Context)
{
	WorldContext = Context;
}

void URPGDungeonMutationTestObserver::ConsumeCompleted(const FString&, const FString&, const FString&,
	const FString&, int64 Generation, bool bSuccess)
{
	++ConsumeReplies;
	bLastConsumeSucceeded = bSuccess;
	LastConsumeGeneration = Generation;
	if (const auto* Web = InspectConsumeGuard.Get())
		bMutationAllowedDuringConsumeReply = Web->CanDispatchConfiguredDungeonConnectionMutation();
}

void URPGDungeonMutationTestObserver::SettlementCompleted(const FString& State, int32, bool bSuccess)
{
	++SettlementReplies;
	bLastSettlementSucceeded = bSuccess;
	LastSettlementState = State;
}

void ARPGDungeonEjectionTestGameMode::BeginPlay()
{
	// Exercise production base lifetime without creating a content boss/encounter.
	ARPGGameModeBase::BeginPlay();
}

void ARPGDungeonEjectionTestGameMode::Logout(AController* Exiting)
{
	++LogoutCount;
	if (const ARPGPlayerController* Controller = Cast<ARPGPlayerController>(Exiting))
	{
		ObservedCleanupConnection = Controller->GetConnectionBeingCleanedUp();
	}
	Super::Logout(Exiting);
}

void ARPGDungeonEjectionTestGameMode::AddTestParticipant(APlayerController* Controller)
{
	Participants.Add(Controller);
	ParticipantRetryStates.Add(Controller).LastPawn = Controller->GetPawn();
}

void ARPGDungeonEjectionTestGameMode::ReplaceConnectionOnIdentityChanged(const FString&)
{
	if (ReplaceConnectionController)
	{
		ReplaceConnectionController->Player = ReplacementConnection;
		ReplaceConnectionController->NetConnection = ReplacementConnection;
	}
}

void URPGDungeonEjectionTestConnection::InitializeNativeConnection(UNetDriver& InDriver)
{
	Driver = &InDriver;
	// InitChannelData is protected but not DLL-exported in the installed engine.
	// No channel/socket is opened; reserve only the control-slot bookkeeping used
	// by Close/CleanUp, and allocate the real driver-owned connection handle.
	Channels.AddDefaulted(1);
	OutReliable.AddDefaulted(1);
	InReliable.AddDefaulted(1);
	PendingOutRec.AddDefaulted(1);
	SetConnectionHandle(UE::Net::FConnectionHandle(InDriver.AllocateConnectionId()));
	SetConnectionState(USOCK_Open);
	SetClientLoginState(EClientLoginState::ReceivedJoin);
}

void ARPGDungeonEjectionTestGameMode::EndMembershipScopeOnIdentityChanged(const FString&)
{
#if WITH_DEV_AUTOMATION_TESTS
	// Synchronous notification invalidates this scope without fabricating a new one.
	ARPGGameModeBase::EndPlay(EEndPlayReason::Quit);
#endif
}
