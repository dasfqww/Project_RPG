#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyNextStep.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyNextStepTest, "ProjectRPG.Online.DungeonLobbyNextStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGOnlineLobbyNextStepTest::RunTest(const FString&)
{
	using State = ERPGOnlineLobbyNextStepState;
	TSet<FName> Available;
	for (const FName Name : {TEXT("LoginSteamButton"), TEXT("LoginDevelopmentButton"), TEXT("OwnedCharacters"),
		TEXT("ContinueWithCharacterButton"),
		TEXT("NewCharacterName"), TEXT("ReloadCharactersButton"), TEXT("CreateDungeonSessionButton"),
		TEXT("DungeonSelectionOptions"), TEXT("DungeonDifficulty"), TEXT("ResumeDungeonSessionButton"),
		TEXT("RefreshDungeonSessionButton"), TEXT("EnterDungeonButton"), TEXT("RequestDungeonDepartureButton"),
		TEXT("RetryDungeonDepartureButton"), TEXT("CancelDungeonDepartureButton"), TEXT("LogoutBackendButton"),
		TEXT("LeaveDungeonSessionButton"), TEXT("AbandonDungeonSessionButton"), TEXT("ConfirmDungeonAbandonmentButton")})
		Available.Add(Name);
	auto Build = [&](State Value) { return FRPGOnlineLobbyNextStepPresentation::Build(Value, Available); };
	for (const auto Value : {State::Unavailable, State::ConfirmingAbandonment, State::Busy, State::AbandonmentPending})
	{
		const auto Guidance = Build(Value);
		TestFalse(TEXT("Blocked states still explain the next step"), Guidance.Text.IsEmpty());
		TestTrue(TEXT("Blocked or confirmation states never recommend execution"), Guidance.TargetControl.IsNone());
	}
	TestEqual(TEXT("Development login is recommended only when actually available"), Build(State::Login).TargetControl, FName(TEXT("LoginDevelopmentButton")));
	Available.Remove(TEXT("LoginDevelopmentButton"));
	TestEqual(TEXT("Normal login recommends the real Steam action"), Build(State::Login).TargetControl, FName(TEXT("LoginSteamButton")));
	TestEqual(TEXT("Reauthentication keeps the safe login action"), Build(State::Reauthenticate).TargetControl, FName(TEXT("LoginSteamButton")));
	TestEqual(TEXT("Loaded character selection precedes creation"), Build(State::Character).TargetControl, FName(TEXT("OwnedCharacters")));
	TestEqual(TEXT("An owned selection guides explicit preparation entry"), Build(State::CharacterSelected).TargetControl, FName(TEXT("ContinueWithCharacterButton")));
	Available.Remove(TEXT("ContinueWithCharacterButton"));
	TestEqual(TEXT("A locked continuation guides a read"), Build(State::CharacterSelected).TargetControl, FName(TEXT("ReloadCharactersButton")));
	Available.Remove(TEXT("OwnedCharacters"));
	TestEqual(TEXT("An empty selectable roster guides name entry, not automatic creation"), Build(State::Character).TargetControl, FName(TEXT("NewCharacterName")));
	TestEqual(TEXT("Invalid names guide the existing input"), Build(State::InvalidCharacterName).TargetControl, FName(TEXT("NewCharacterName")));
	Available.Remove(TEXT("NewCharacterName"));
	TestEqual(TEXT("A locked character input falls back to a read"), Build(State::Character).TargetControl, FName(TEXT("ReloadCharactersButton")));
	TestEqual(TEXT("Validated creation is an explicit existing action"), Build(State::SessionCreation).TargetControl, FName(TEXT("CreateDungeonSessionButton")));
	Available.Remove(TEXT("CreateDungeonSessionButton"));
	TestEqual(TEXT("Unavailable creation guides selection instead"), Build(State::SessionCreation).TargetControl, FName(TEXT("DungeonSelectionOptions")));
	TestEqual(TEXT("Recruiting may guide a currently enabled leader request"), Build(State::Recruiting).TargetControl, FName(TEXT("RequestDungeonDepartureButton")));
	Available.Remove(TEXT("RequestDungeonDepartureButton"));
	TestEqual(TEXT("Members or locked leaders receive a refresh, not guessed authority"), Build(State::Recruiting).TargetControl, FName(TEXT("RefreshDungeonSessionButton")));
	for (const auto Value : {State::DepartureQueued, State::ServerLoading, State::SettlementPending,
		State::UnknownSession, State::MembershipCheck, State::RequestFailed, State::SteamParty})
		TestEqual(TEXT("Waiting, blocked and uncertain snapshots prefer a read"), Build(Value).TargetControl, FName(TEXT("RefreshDungeonSessionButton")));
	TestEqual(TEXT("A ready server recommends only enabled entry"), Build(State::ServerStarted).TargetControl, FName(TEXT("EnterDungeonButton")));
	Available.Remove(TEXT("EnterDungeonButton"));
	TestEqual(TEXT("Running without admissibility does not recommend entry"), Build(State::ServerStarted).TargetControl, FName(TEXT("RefreshDungeonSessionButton")));
	TestEqual(TEXT("Unconfirmed departure prioritizes its original-command recovery"), Build(State::DepartureUnconfirmed).TargetControl, FName(TEXT("RetryDungeonDepartureButton")));
	TestEqual(TEXT("Storage errors do not recommend mutating retry"), Build(State::DepartureStorageBlocked).TargetControl, FName(TEXT("RefreshDungeonSessionButton")));
	Available.Remove(TEXT("RetryDungeonDepartureButton"));
	TestEqual(TEXT("An existing session prefers refresh over rediscovery"), Build(State::DepartureUnconfirmed).TargetControl, FName(TEXT("RefreshDungeonSessionButton")));
	Available.Remove(TEXT("RefreshDungeonSessionButton"));
	TestEqual(TEXT("Restored command without retry first recommends owner read"), Build(State::DepartureUnconfirmed).TargetControl, FName(TEXT("ResumeDungeonSessionButton")));
	Available.Add(TEXT("RefreshDungeonSessionButton"));
	Available.Remove(TEXT("ResumeDungeonSessionButton"));
	TestEqual(TEXT("Unavailable resume falls back to existing session read"), Build(State::DepartureUnconfirmed).TargetControl, FName(TEXT("RefreshDungeonSessionButton")));
	Available.Empty();
	// Even enabled dangerous controls cannot become an implicit primary action.
	for (const FName Name : {TEXT("LogoutBackendButton"), TEXT("LeaveDungeonSessionButton"), TEXT("CancelDungeonDepartureButton"),
		TEXT("AbandonDungeonSessionButton"), TEXT("ConfirmDungeonAbandonmentButton")}) Available.Add(Name);
	for (const auto Value : {State::Unavailable, State::ConfirmingAbandonment, State::Busy, State::Login,
		State::Reauthenticate, State::Character, State::InvalidCharacterName, State::AbandonmentPending,
		State::DepartureStorageBlocked, State::DepartureUnconfirmed, State::MembershipCheck, State::RequestFailed,
		State::SessionCreation, State::SteamParty, State::Recruiting, State::DepartureQueued,
		State::ServerLoading, State::ServerStarted, State::SettlementPending, State::UnknownSession, State::CharacterSelected})
	{
		const auto Guidance = Build(Value);
		TestFalse(TEXT("All presentation categories have localized guidance"), Guidance.Text.IsEmpty());
		TestTrue(TEXT("Missing safe actions never fall back to a destructive action"), Guidance.TargetControl.IsNone());
		TestTrue(TEXT("No enabled safe control means no invented target"),
			FRPGOnlineLobbyNextStepPresentation::Build(Value, {}).TargetControl.IsNone());
	}
	return true;
}
#endif
