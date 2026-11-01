#include "UI/Lobby/RPGOnlineLobbyNextStep.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include <initializer_list>

FRPGOnlineLobbyNextStep FRPGOnlineLobbyNextStepPresentation::Build(ERPGOnlineLobbyNextStepState State,
	const TSet<FName>& AvailableControls)
{
	auto Step = [&AvailableControls](const FText& Text, std::initializer_list<FName> Candidates)
	{
		FRPGOnlineLobbyNextStep Result;
		Result.Text = Text;
		for (FName Candidate : Candidates)
			if (AvailableControls.Contains(Candidate)) { Result.TargetControl = Candidate; break; }
		return Result;
	};
	using StateType = ERPGOnlineLobbyNextStepState;
	switch (State)
	{
	case StateType::Unavailable:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextUnavailable", "다음 단계: 온라인 서비스를 사용할 수 있는 로비에서 다시 확인하세요."), {});
	case StateType::ConfirmingAbandonment:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextConfirmation", "다음 단계: 열린 포기 확인창을 먼저 확인하세요. 원하지 않으면 취소할 수 있습니다."), {});
	case StateType::Busy:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextBusy", "다음 단계: 현재 요청 또는 이동이 끝날 때까지 기다리세요."), {});
	case StateType::Login:
	case StateType::Reauthenticate:
	{
		auto Result = Step(State == StateType::Reauthenticate
			? NSLOCTEXT("RPGOnlineLobby", "NextReauthenticate", "다음 단계: 같은 계정으로 다시 로그인하세요. 캐릭터와 던전 정보는 유지됩니다.")
			: NSLOCTEXT("RPGOnlineLobby", "NextLogin", "다음 단계: Steam으로 로그인하세요."),
			{TEXT("LoginDevelopmentButton"), TEXT("LoginSteamButton")});
		if (Result.TargetControl == TEXT("LoginDevelopmentButton"))
			Result.Text = State == StateType::Reauthenticate
				? NSLOCTEXT("RPGOnlineLobby", "NextDevelopmentReauthenticate", "다음 단계: 유지된 개발 계정으로 다시 로그인하세요. 실제 Steam 인증 경로는 아닙니다.")
				: NSLOCTEXT("RPGOnlineLobby", "NextDevelopmentLogin", "다음 단계: 개발용 SteamID를 입력한 뒤 개발 백엔드 로그인 버튼을 누르세요.");
		return Result;
	}
	case StateType::Character:
	{
		auto Result = Step(NSLOCTEXT("RPGOnlineLobby", "NextCharacter", "다음 단계: 내 캐릭터를 선택하세요. 선택하면 기존 던전이 있는지 서버에서 조회합니다."),
			{TEXT("OwnedCharacters"), TEXT("NewCharacterName"), TEXT("ReloadCharactersButton")});
		if (Result.TargetControl == TEXT("NewCharacterName"))
			Result.Text = NSLOCTEXT("RPGOnlineLobby", "NextCreateCharacter", "다음 단계: 새 캐릭터 이름을 입력한 뒤 캐릭터 생성 버튼을 누르세요.");
		else if (Result.TargetControl != TEXT("OwnedCharacters"))
			Result.Text = NSLOCTEXT("RPGOnlineLobby", "NextCharacterRefresh", "다음 단계: 캐릭터 목록을 새로고침해 선택할 캐릭터를 확인하세요.");
		return Result;
	}
	case StateType::InvalidCharacterName:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextFixCharacterName", "다음 단계: 캐릭터 이름을 앞뒤 공백 제외 2~20자로 입력하세요. 줄바꿈과 제어 문자는 사용할 수 없습니다."), {TEXT("NewCharacterName")});
	case StateType::CharacterSelected:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextContinueCharacter", "다음 단계: 선택한 캐릭터를 확인하고 원정 준비로 이동하세요."),
			{TEXT("ContinueWithCharacterButton"), TEXT("ReloadCharactersButton")});
	case StateType::AbandonmentPending:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextAbandonmentPending", "다음 단계: 기존 포기 요청의 확인 안내를 따르세요. 서버 확인 전에는 새 던전이나 보상 상태를 확정하지 않습니다."), {});
	case StateType::DepartureStorageBlocked:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextDepartureStorage", "다음 단계: 저장 공간과 권한을 확인한 뒤 기존 세션을 복원하거나 새로고침하세요. 새 출발 요청은 만들지 마세요."),
			{TEXT("RefreshDungeonSessionButton"), TEXT("ResumeDungeonSessionButton")});
	case StateType::DepartureUnconfirmed:
	{
		auto Result = Step(NSLOCTEXT("RPGOnlineLobby", "NextOriginalDeparture", "다음 단계: 원래 출발 또는 취소 요청 재확인 버튼으로 같은 요청의 결과를 확인하세요."),
			{TEXT("RetryDungeonDepartureButton"), TEXT("RefreshDungeonSessionButton"), TEXT("ResumeDungeonSessionButton")});
		if (Result.TargetControl != TEXT("RetryDungeonDepartureButton"))
			Result.Text = NSLOCTEXT("RPGOnlineLobby", "NextDepartureReadFirst", "다음 단계: 기존 세션을 복원하거나 새로고침하세요. 저장된 원래 출발/취소 요청을 유지하며 새 요청은 만들지 않습니다.");
		return Result;
	}
	case StateType::MembershipCheck:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextMembershipCheck", "다음 단계: 세션을 새로고침하거나 복원해 현재 참가 자격을 확인하세요. 이전 조회만으로 입장할 수는 없습니다."),
			{TEXT("RefreshDungeonSessionButton"), TEXT("ResumeDungeonSessionButton"), TEXT("ReloadCharactersButton")});
	case StateType::RequestFailed:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextFailedRequest", "다음 단계: 위 오류 안내를 확인한 뒤 세션 또는 캐릭터 정보를 새로 조회하세요."),
			{TEXT("RefreshDungeonSessionButton"), TEXT("ResumeDungeonSessionButton"), TEXT("ReloadCharactersButton")});
	case StateType::SessionCreation:
	{
		auto Result = Step(NSLOCTEXT("RPGOnlineLobby", "NextCreateSession", "다음 단계: 선택한 던전과 난이도를 확인한 뒤 던전 세션을 생성하세요. Steam 파티 참가도 선택할 수 있습니다."),
			{TEXT("CreateDungeonSessionButton"), TEXT("DungeonSelectionOptions"), TEXT("DungeonDifficulty")});
		if (Result.TargetControl != TEXT("CreateDungeonSessionButton"))
			Result.Text = NSLOCTEXT("RPGOnlineLobby", "NextDungeonSelection", "다음 단계: 사용 가능한 던전과 난이도를 먼저 확인하세요. 현재 설정으로는 새 세션을 생성할 수 없습니다.");
		return Result;
	}
	case StateType::SteamParty:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextSteamParty", "다음 단계: 참가 중인 Steam 파티의 던전 정보를 확인하세요. 파티에 속한 동안 별도 세션은 만들지 않습니다."), {TEXT("RefreshDungeonSessionButton")});
	case StateType::Recruiting:
	{
		auto Result = Step(NSLOCTEXT("RPGOnlineLobby", "NextRecruiting", "다음 단계: 파티 준비가 됐다면 방장이 출발을 요청하세요. 서버가 출발 조건을 다시 확인합니다."),
			{TEXT("RequestDungeonDepartureButton"), TEXT("RefreshDungeonSessionButton")});
		if (Result.TargetControl != TEXT("RequestDungeonDepartureButton"))
			Result.Text = NSLOCTEXT("RPGOnlineLobby", "NextRecruitingRead", "다음 단계: 파티 정보를 새로고침하세요. 출발은 방장의 요청과 서버 조건 확인이 필요합니다.");
		return Result;
	}
	case StateType::DepartureQueued:
	case StateType::ServerLoading:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextServerPreparation", "다음 단계: 서버 준비를 기다리세요. 필요하면 파티 / 서버 준비를 새로고침할 수 있습니다."), {TEXT("RefreshDungeonSessionButton")});
	case StateType::ServerStarted:
	{
		auto Result = Step(NSLOCTEXT("RPGOnlineLobby", "NextEnter", "다음 단계: 준비된 던전 입장 버튼을 누르세요. 입장 시 서버가 참가 자격과 티켓을 확인합니다."),
			{TEXT("EnterDungeonButton"), TEXT("RefreshDungeonSessionButton")});
		if (Result.TargetControl != TEXT("EnterDungeonButton"))
			Result.Text = NSLOCTEXT("RPGOnlineLobby", "NextServerNotAdmissible", "다음 단계: 세션을 새로고침하세요. 서버 실행 상태만으로 입장 조건이 충족되지는 않습니다.");
		return Result;
	}
	case StateType::SettlementPending:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextSettlement", "다음 단계: 서버 정산 완료를 기다리며 세션을 새로고침하세요. 아직 보상 지급 완료를 뜻하지 않습니다."), {TEXT("RefreshDungeonSessionButton")});
	default:
		return Step(NSLOCTEXT("RPGOnlineLobby", "NextUnknownSession", "다음 단계: 세션을 새로고침해 서버 상태를 확인하세요. 확인되지 않은 상태로 새 출발이나 입장을 진행하지 않습니다."), {TEXT("RefreshDungeonSessionButton")});
	}
}

FRPGOnlineLobbyNextStep URPGOnlineLobbyWidget::BuildNextStepGuidance() const
{
	using StateType = ERPGOnlineLobbyNextStepState;
	const auto View = CurrentPresentation();
	const auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	StateType State = StateType::Unavailable;
	if (Web && Auth && Flow && Lobby && Lobby->IsLobbyController(GetOwningPlayer()))
	{
		const auto Session = Flow->GetCurrentDungeonSession();
		if (bAbandonmentConfirmationOpen) State = StateType::ConfirmingAbandonment;
		else if (View.bBusy) State = StateType::Busy;
		else if (!Web->HasAccessToken()) State = Web->GetAuthenticatedSteamId().IsEmpty() ? StateType::Login : StateType::Reauthenticate;
		else if (UIError == TEXT("invalid_character_name")) State = StateType::InvalidCharacterName;
		else if (!bPreparationOpen || !HasOwnedSelection()) State = HasOwnedSelection() ? StateType::CharacterSelected : StateType::Character;
		else if (Flow->HasQueuedDungeonAbandonment() || Flow->HasUnresolvedDungeonAbandonment()) State = StateType::AbandonmentPending;
		else if (Flow->HasDepartureRecoveryError()) State = StateType::DepartureStorageBlocked;
		else if (Flow->HasUnresolvedDeparture()) State = StateType::DepartureUnconfirmed;
		else if (!Flow->GetDungeonAdmissionBlockReason().IsEmpty()) State = StateType::MembershipCheck;
		else if ((!UIError.IsEmpty() && UIError != TEXT("backend_logged_out"))
			|| (!Flow->GetLastErrorCode().IsEmpty() && Flow->GetLastErrorCode() != TEXT("dungeon_abandoned"))
			|| (Steam && !Steam->GetLastErrorCode().IsEmpty())) State = StateType::RequestFailed;
		else if (!HasOwnedSelection()) State = StateType::Character;
		else if ((Session.DungeonSessionId.IsEmpty() && Session.State.IsEmpty()) || Session.State == TEXT("Cleared")
			|| Session.State == TEXT("Failed") || Session.State == TEXT("Closed"))
			State = Steam && Steam->IsInPartyLobby() ? StateType::SteamParty : StateType::SessionCreation;
		else if (Session.State == TEXT("Waiting")) State = Session.DepartureRequestedAt.IsEmpty() ? StateType::Recruiting : StateType::DepartureQueued;
		else if (Session.State == TEXT("Loading")) State = StateType::ServerLoading;
		else if (Session.State == TEXT("InProgress")) State = StateType::ServerStarted;
		else if (Session.State == TEXT("SettlementPending")) State = StateType::SettlementPending;
		else State = StateType::UnknownSession;
	}
	TSet<FName> Available;
	auto AddAvailable = [&Available](const UWidget* Control)
	{
		if (!Control || !Control->GetIsEnabled() || !Control->IsVisible()) return;
		for (auto* Parent = Control->GetParent(); Parent; Parent = Parent->GetParent())
			if (!Parent->GetIsEnabled() || !Parent->IsVisible()) return;
		Available.Add(Control->GetFName());
	};
	if (GetIsEnabled() && IsVisible())
	{
		for (const auto& Pair : Buttons) AddAvailable(Pair.Value);
		AddAvailable(CharactersBox); AddAvailable(CharacterName); AddAvailable(DungeonsBox); AddAvailable(DifficultyBox);
	}
	return FRPGOnlineLobbyNextStepPresentation::Build(State, Available);
}

FText URPGOnlineLobbyWidget::GetNextStepText() const { return BuildNextStepGuidance().Text; }
FName URPGOnlineLobbyWidget::GetNextStepControlName() const { return BuildNextStepGuidance().TargetControl; }

void URPGOnlineLobbyWidget::RefreshNextStepPresentation()
{
	const auto Guidance = BuildNextStepGuidance();
	if (NextStepText) NextStepText->SetText(Guidance.Text);
	for (const auto& Pair : Buttons)
		if (Pair.Value)
		{
			const bool bPrimary = Pair.Key == TEXT("EnterDungeonButton") || Pair.Key == TEXT("ContinueWithCharacterButton")
				|| Pair.Key == TEXT("LoginSteamButton");
			const FLinearColor Base = bPrimary ? FLinearColor(.08f, .38f, .43f) : FLinearColor::White;
			Pair.Value->SetBackgroundColor(Pair.Key == Guidance.TargetControl ? FLinearColor(.32f, .24f, .07f) : Base);
		}
}
