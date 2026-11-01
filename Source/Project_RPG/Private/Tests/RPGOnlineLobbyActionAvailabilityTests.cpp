#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyActionAvailability.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyActionAvailabilityTest, "ProjectRPG.Online.DungeonLobbyActionAvailability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGOnlineLobbyActionAvailabilityTest::RunTest(const FString&)
{
	using Block = ERPGOnlineLobbyBlockReason;
	using Presentation = FRPGOnlineLobbyActionAvailability;
	TestTrue(TEXT("No block has no text"), Presentation::Text(Block::None).IsEmpty());
	TestEqual(TEXT("Enabled result overrides all explanatory blockers"),
		Presentation::Explain(true, Block::Unavailable, {{false, Block::LoginRequired}}), Block::None);
	TestEqual(TEXT("Enabled actions do not retain stale generic reasons"), Presentation::Explain(true, Block::None), Block::None);
	TestEqual(TEXT("Common availability precedes per-action hints"),
		Presentation::Explain(false, Block::Busy, {{false, Block::CharacterRequired}}), Block::Busy);
	TestEqual(TEXT("Confirmation remains the first reason when supplied by the screen"),
		Presentation::Explain(false, Block::ConfirmationOpen, {{false, Block::LeaderRequired}}), Block::ConfirmationOpen);
	TestEqual(TEXT("First known unmet prerequisite is used"),
		Presentation::Explain(false, Block::None, {{true, Block::LoginRequired}, {false, Block::CharacterRequired},
			{false, Block::LeaderRequired}}), Block::CharacterRequired);
	TestEqual(TEXT("Expired authentication can guide same-account login"),
		Presentation::Explain(false, Block::None, {{false, Block::ReauthenticationRequired},
			{false, Block::MemberSessionRequired}}), Block::ReauthenticationRequired);
	TestEqual(TEXT("Original departure recovery takes priority over new-action hints"),
		Presentation::Explain(false, Block::None, {{false, Block::DepartureUnconfirmed},
			{false, Block::DepartureQueued}}), Block::DepartureUnconfirmed);
	TestEqual(TEXT("Read-only explanation never invents a detailed cause"),
		Presentation::Explain(false, Block::None, {{true, Block::LeaderRequired}}), Block::StateCheckRequired);
	TestEqual(TEXT("A missing explanation cannot silently clear a disabled result"),
		Presentation::Explain(false, Block::None, {{false, Block::None}}), Block::StateCheckRequired);
	for (const auto Reason : {Block::Unavailable, Block::ConfirmationOpen, Block::Busy, Block::LoginRequired,
		Block::ReauthenticationRequired, Block::AlreadyAuthenticated, Block::LogoutCredentialRequired,
		Block::CharacterRequired, Block::ActiveSession, Block::MemberSessionRequired, Block::SteamPartyActive,
		Block::SteamPartyRequired, Block::WaitingRequired, Block::LeaderRequired, Block::LobbyProtocolRequired,
		Block::DepartureQueued, Block::DepartureNotQueued, Block::DepartureUnconfirmed, Block::NoPendingDeparture,
		Block::DepartureStorageBlocked, Block::AbandonmentUnconfirmed, Block::AdmissionBlocked, Block::ServerNotReady,
		Block::DungeonSelectionRequired, Block::PartySelectionRequired, Block::PartyNotJoinable,
		Block::InviteRequired, Block::DevelopmentOnly, Block::StateCheckRequired, Block::SteamDisabled, Block::UnsavedConfiguration})
	{
		TestFalse(TEXT("Every blocked category has localized explanatory text"), Presentation::Text(Reason).IsEmpty());
		TestEqual(TEXT("Explanatory categories cannot veto an enabled action"),
			Presentation::Explain(true, Reason, {{false, Reason}}), Block::None);
	}
	return true;
}
#endif
