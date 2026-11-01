#include "Tests/RPGDungeonFlowE2EProbe.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/RPGDungeonDepartureJournal.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

// Invoked only by the existing development/no-Steam E2E opt-in. No private flow state or credential is injected.
bool URPGDungeonFlowE2EClient::TickDepartureRestartProbe()
{
	auto* GI = GetGameInstance();
	auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Web = GI->GetSubsystem<UHttpWebManager>();
	auto* Widget = GI->GetSubsystem<URPGOnlineLobbySubsystem>()->GetLobbyWidget();
	const bool bRequest = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureRequestRecovery"));
	const bool bPrecommit = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EDeparturePrecommitRecovery"));
	FGuid Parsed;
	if (!bLobbyUI || !bLobbyLeader || !FGuid::Parse(SessionId, Parsed) || !Parsed.IsValid()) return Fail(TEXT("DepartureRestartArguments"));
	if (!Widget) return true;
	if (Stage == 0)
	{
		if (bLoginStarted) return true;
		auto* Login = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton")));
		auto* Input = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSteamId")));
		if (!Login || !Input || !Login->GetIsEnabled()) return true;
		if (Web->HasAccessToken() || !Web->GetAuthenticatedSteamId().IsEmpty()
			|| !Flow->GetSelectedCharacterId().IsEmpty() || Flow->HasActiveDungeonSession() || Flow->HasUnresolvedDeparture())
			return Fail(TEXT("DepartureRestartNotFresh"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_COLD_START EMPTY_STATE Credential=0 Selection=0 Session=0 Pending=0"));
		Input->SetText(FText::FromString(SteamId));
		bLoginStarted = true;
		Login->OnClicked.Broadcast();
		return true; // The normal login callback only advances Stage to 1; the widget loads its own roster.
	}
	if (Stage != 1) return Fail(TEXT("DepartureRestartStage"));
	if (!Web->HasAccessToken() || !Web->GetAuthenticatedSteamId().Equals(SteamId, ESearchCase::CaseSensitive))
		return Fail(TEXT("DepartureRestartLogin"));
	auto* Selector = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters")));
	if (!Selector) return true;
	const FString Option = Widget->GetCharacterOption(CharacterId);
	if (DeparturePhase == 0)
	{
		if (Option.IsEmpty() || !Selector->GetIsEnabled()) return true;
		if (Flow->HasUnresolvedDeparture() || Flow->HasActiveDungeonSession()) return Fail(TEXT("DepartureRestartPrematureRestore"));
		DeparturePhase = 20;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_COLD_START AUTHENTICATED FreshLogin=1 OwnedRoster=1 Pending=0"));
	}
	FRPGDungeonDepartureJournal Journal(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Online/DepartureCommands")),
		Web->GetApiUrl(), SteamId, CharacterId);
	if (DeparturePhase == 20)
	{
		FString Gate;
		FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2ERecoveryRetryFile="), Gate);
		if (Gate.IsEmpty()) return Fail(TEXT("DepartureRestartGate"));
		if (!IFileManager::Get().FileExists(*Gate)) return true;
		FRPGDungeonDepartureCommand Stored;
		if (Journal.Load(Stored) != ERPGDepartureJournalLoad::Found || Stored.SessionId != SessionId || Stored.bCancel == bRequest)
			return Fail(TEXT("DepartureRestartJournalScope"));
		FirstDepartureRequestId = Stored.RequestId; DepartureRestartRevision = Stored.LobbyRevision;
		DeparturePhase = 21;
		Selector->SetSelectedOption(Option); // Real selection -> journal restore -> real owner-scoped Resume GET.
		if (!Flow->HasUnresolvedDeparture() || Flow->HasDepartureRecoveryError()
			|| Flow->GetSelectedCharacterId() != CharacterId || Flow->HasActiveDungeonSession())
			return Fail(TEXT("DepartureRestartSelectionDidNotRestore"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_COLD_START RESTORED OriginalScope=1 OriginalCommand=1 NoFabricatedSession=1"));
		return true;
	}
	if (DeparturePhase == 21 && bPrecommit)
	{
		if (Flow->IsRequestInFlight()) return true;
		const auto Session = Flow->GetCurrentDungeonSession();
		FRPGDungeonDepartureCommand Unchanged;
		auto* Request = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RequestDungeonDepartureButton")));
		auto* Cancel = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelDungeonDepartureButton")));
		auto* Retry = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RetryDungeonDepartureButton")));
		if (Flow->GetFlowState() == ERPGDungeonClientFlowState::Error || !Flow->HasUnresolvedDeparture() || Flow->HasDepartureRecoveryError()
			|| Session.DungeonSessionId != SessionId || Session.State != TEXT("Waiting") || Session.LeaderCharacterId != CharacterId
			|| Session.DepartureRequestedAt.IsEmpty() != bRequest || Session.LobbyRevision != DepartureRestartRevision + (bRequest ? 0 : 1)
			|| (bRequest ? !Session.DepartureRequestId.IsEmpty() : Session.DepartureRequestId != FirstDepartureRequestId
				|| Session.DepartureRequestRevision != DepartureRestartRevision)
			|| Journal.Load(Unchanged) != ERPGDepartureJournalLoad::Found || Unchanged.RequestId != FirstDepartureRequestId
			|| Unchanged.LobbyRevision != DepartureRestartRevision || Unchanged.bCancel == bRequest)
			return Fail(TEXT("DeparturePrecommitOwnerReadDiscardedCommand"));
		if (!Request || !Cancel || !Retry || Request->GetIsEnabled() || Cancel->GetIsEnabled() || !Retry->GetIsEnabled())
			return Fail(TEXT("DeparturePrecommitRetryControls"));
		DeparturePhase = 22;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_COLD_START PRECOMMIT_UNCONFIRMED OwnerRead=1 OriginalCommand=1 RetryEnabled=1 NewActionsLocked=1"));
		return true;
	}
	if (DeparturePhase == 22)
	{
		FString Gate;
		FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EDepartureCommitRetryFile="), Gate);
		if (Gate.IsEmpty()) return Fail(TEXT("DeparturePrecommitRetryGate"));
		if (!IFileManager::Get().FileExists(*Gate)) return true;
		auto* Retry = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RetryDungeonDepartureButton")));
		if (!Flow->HasUnresolvedDeparture() || Flow->IsRequestInFlight() || !Retry || !Retry->GetIsEnabled())
			return Fail(TEXT("DeparturePrecommitRetryNotAvailable"));
		DeparturePhase = 23;
		Retry->OnClicked.Broadcast(); // Explicit live button -> existing RetryDeparture, preserving the disk tuple.
		if (!Flow->IsRequestInFlight()) return Fail(TEXT("DeparturePrecommitRetryNotDispatched"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_COLD_START RETRY_DISPATCHED LiveButton=1 OriginalIntent=1"));
		return true;
	}
	if (DeparturePhase == 21 || DeparturePhase == 23)
	{
		if (Flow->IsRequestInFlight()) return true;
		const auto Session = Flow->GetCurrentDungeonSession();
		FRPGDungeonDepartureCommand Removed;
		auto* Request = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RequestDungeonDepartureButton")));
		auto* Cancel = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelDungeonDepartureButton")));
		auto* Retry = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RetryDungeonDepartureButton")));
		if (Flow->GetFlowState() == ERPGDungeonClientFlowState::Error || Flow->HasUnresolvedDeparture() || Flow->HasDepartureRecoveryError()
			|| Session.DungeonSessionId != SessionId || Session.State != TEXT("Waiting") || Session.DepartureRequestedAt.IsEmpty() == bRequest
			|| Session.DepartureRequestId != FirstDepartureRequestId || Session.DepartureRequestRevision != DepartureRestartRevision
			|| !Session.Members.ContainsByPredicate([this](const FRPGDungeonSessionMember& Member) { return Member.CharacterId == CharacterId; })
			|| Journal.Load(Removed) != ERPGDepartureJournalLoad::Missing) return Fail(TEXT("DepartureRestartOwnerReadFailed"));
		if (!Request || !Cancel || !Retry || Request->GetIsEnabled() == bRequest || Cancel->GetIsEnabled() != bRequest || Retry->GetIsEnabled())
			return Fail(TEXT("DepartureRestartControls"));
		if (bPrecommit)
		{
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_COLD_START REPLAY_COMMITTED NewProcess=1 OriginalCommand=1 JournalRemoved=1 Mutation=1"));
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_DEPARTURE_COLD_START RECOVERED NewProcess=1 OwnerRead=1 JournalRemoved=1 NoMutation=1"));
		}
		Stage = 55;
		return false;
	}
	return true;
}
