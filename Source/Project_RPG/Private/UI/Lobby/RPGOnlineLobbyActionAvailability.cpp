#include "UI/Lobby/RPGOnlineLobbyActionAvailability.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

ERPGOnlineLobbyBlockReason FRPGOnlineLobbyActionAvailability::Explain(bool bEnabled,
	ERPGOnlineLobbyBlockReason CommonReason, std::initializer_list<FRPGOnlineLobbyActionRequirement> Requirements)
{
	using Reason = ERPGOnlineLobbyBlockReason;
	if (bEnabled) return Reason::None;
	if (CommonReason != Reason::None) return CommonReason;
	for (const auto& Requirement : Requirements)
		if (!Requirement.bSatisfied && Requirement.MissingReason != Reason::None) return Requirement.MissingReason;
	return Reason::StateCheckRequired;
}

FText FRPGOnlineLobbyActionAvailability::Text(ERPGOnlineLobbyBlockReason Reason)
{
	using Block = ERPGOnlineLobbyBlockReason;
	switch (Reason)
	{
	case Block::None: return FText::GetEmpty();
	case Block::Unavailable: return NSLOCTEXT("RPGOnlineLobby", "BlockUnavailable", "온라인 서비스를 사용할 수 있는 로비에서만 가능합니다.");
	case Block::ConfirmationOpen: return NSLOCTEXT("RPGOnlineLobby", "BlockConfirmation", "열린 확인창에서 확인하거나 취소한 뒤 이용하세요.");
	case Block::Busy: return NSLOCTEXT("RPGOnlineLobby", "BlockBusy", "현재 요청 또는 이동이 끝난 뒤 이용하세요.");
	case Block::LoginRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockLogin", "먼저 로그인하세요.");
	case Block::ReauthenticationRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockReauthenticate", "같은 계정으로 다시 로그인하세요. 기존 던전 정보는 유지됩니다.");
	case Block::AlreadyAuthenticated: return NSLOCTEXT("RPGOnlineLobby", "BlockAlreadyAuthenticated", "이미 로그인되어 있습니다.");
	case Block::LogoutCredentialRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockLogoutCredential", "로그아웃을 요청할 계정 자격이 없습니다.");
	case Block::CharacterRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockCharacter", "내 캐릭터를 먼저 선택하세요.");
	case Block::ActiveSession: return NSLOCTEXT("RPGOnlineLobby", "BlockActiveSession", "진행 중이거나 상태 확인이 필요한 던전이 있습니다. 기존 세션을 먼저 확인하세요.");
	case Block::MemberSessionRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockMemberSession", "현재 캐릭터가 참가한 세션을 먼저 복원하거나 확인하세요.");
	case Block::SteamPartyActive: return NSLOCTEXT("RPGOnlineLobby", "BlockSteamPartyActive", "Steam 파티에 참가 중이라 별도로 처리할 수 없습니다. 파티 상태를 먼저 확인하세요.");
	case Block::SteamPartyRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockSteamPartyRequired", "참가 중인 Steam 파티가 필요합니다.");
	case Block::WaitingRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockWaiting", "대기 중인 던전 세션에서만 가능합니다.");
	case Block::LeaderRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockLeader", "현재 던전의 방장만 요청할 수 있습니다.");
	case Block::LobbyProtocolRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockLobbyProtocol", "방장 또는 로비 버전 정보가 없어 출발 조건을 확인할 수 없습니다. 세션을 새로고침하세요.");
	case Block::DepartureQueued: return NSLOCTEXT("RPGOnlineLobby", "BlockDepartureQueued", "이미 출발을 요청한 상태입니다. 서버 준비 상태를 확인하세요.");
	case Block::DepartureNotQueued: return NSLOCTEXT("RPGOnlineLobby", "BlockDepartureNotQueued", "취소할 출발 요청이 없습니다.");
	case Block::DepartureUnconfirmed: return NSLOCTEXT("RPGOnlineLobby", "BlockDepartureUnconfirmed", "원래 출발/취소 요청의 결과가 미확정입니다. 같은 요청을 재확인하거나 세션을 조회하세요.");
	case Block::NoPendingDeparture: return NSLOCTEXT("RPGOnlineLobby", "BlockNoPendingDeparture", "재확인할 미확정 출발/취소 요청이 없습니다.");
	case Block::DepartureStorageBlocked: return NSLOCTEXT("RPGOnlineLobby", "BlockDepartureStorage", "출발 기록 복구/정리를 확인해야 합니다. 저장 공간과 권한을 확인하고 기존 세션을 조회하세요.");
	case Block::AbandonmentUnconfirmed: return NSLOCTEXT("RPGOnlineLobby", "BlockAbandonmentUnconfirmed", "기존 포기 요청의 결과를 먼저 확인해야 합니다. 새 포기 요청을 만들지 마세요.");
	case Block::AdmissionBlocked: return NSLOCTEXT("RPGOnlineLobby", "BlockAdmission", "현재 참가 자격이 확인되지 않았습니다. 같은 계정으로 세션을 새로고침하세요.");
	case Block::ServerNotReady: return NSLOCTEXT("RPGOnlineLobby", "BlockServerNotReady", "입장 가능한 서버 준비 정보가 아직 확인되지 않았습니다. 세션을 새로고침하세요.");
	case Block::DungeonSelectionRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockDungeonSelection", "사용 가능한 던전과 난이도를 먼저 확인하세요.");
	case Block::PartySelectionRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockPartySelection", "검색 결과에서 참가할 Steam 파티를 먼저 선택하세요.");
	case Block::PartyNotJoinable: return NSLOCTEXT("RPGOnlineLobby", "BlockPartyNotJoinable", "선택한 Steam 파티는 현재 참가할 수 없습니다. 검색 결과를 새로고침하세요.");
	case Block::InviteRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockInvite", "아직 참가할 Steam 초대가 없습니다.");
	case Block::DevelopmentOnly: return NSLOCTEXT("RPGOnlineLobby", "BlockDevelopmentOnly", "명시적으로 허용된 개발 테스트에서만 가능합니다.");
	case Block::SteamDisabled: return NSLOCTEXT("RPGOnlineLobby", "BlockSteamDisabled", "이 실행에서는 Steam 접속이 꺼져 있습니다. Steam을 활성화해 게임을 다시 실행하세요.");
	case Block::UnsavedConfiguration: return NSLOCTEXT("RPGOnlineLobby", "BlockUnsavedConfiguration", "스킬 구성의 변경을 저장하거나 되돌린 뒤 캐릭터를 전환하세요.");
	case Block::CreationClassRequired: return NSLOCTEXT("RPGOnlineLobby", "BlockCreationClassRequired", "생성 가능한 직업을 불러온 뒤 직업을 선택하세요.");
	default: return NSLOCTEXT("RPGOnlineLobby", "BlockStateCheck", "현재 상태에서는 사용할 수 없습니다. 위 상태 안내와 세션 정보를 확인하세요.");
	}
}

FText URPGOnlineLobbyWidget::GetActionUnavailableReason(FName ButtonName) const
{
	const auto* Button = Buttons.FindRef(ButtonName).Get();
	if (!Button || Button->GetIsEnabled() || !Button->IsVisible() || !IsVisible()) return FText::GetEmpty();
	for (auto* Parent = Button->GetParent(); Parent; Parent = Parent->GetParent())
		if (!Parent->IsVisible()) return FText::GetEmpty();
	const auto* Reason = ActionBlockReasons.Find(ButtonName);
	return FRPGOnlineLobbyActionAvailability::Text(Reason ? *Reason : ERPGOnlineLobbyBlockReason::StateCheckRequired);
}

void URPGOnlineLobbyWidget::RefreshActionUnavailablePresentation()
{
	for (const auto& Pair : ActionUnavailableTexts)
		if (Pair.Value)
		{
			const auto Reason = GetActionUnavailableReason(Pair.Key);
			Pair.Value->SetText(Reason);
			Pair.Value->SetVisibility(Reason.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
}
