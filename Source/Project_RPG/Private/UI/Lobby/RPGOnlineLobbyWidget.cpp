#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"
#include "UI/Lobby/RPGEquipmentGrowthWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/RPGConfirmationInput.h"
#include "UI/RPGMenuFocus.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UI/Lobby/RPGOnlineLobbyLayout.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/Lobby/RPGOnlineLobbyDevelopmentProfile.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "TimerManager.h"

namespace
{
	bool Same(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	bool Active(const FString& State)
	{
		return Same(State, TEXT("Waiting")) || Same(State, TEXT("Loading"))
			|| Same(State, TEXT("InProgress")) || Same(State, TEXT("SettlementPending"));
	}
	bool Unfinished(const FRPGDungeonSession& Session)
	{
		return Active(Session.State) || ((!Session.DungeonSessionId.IsEmpty() || !Session.State.IsEmpty())
			&& !Same(Session.State, TEXT("Cleared")) && !Same(Session.State, TEXT("Failed")) && !Same(Session.State, TEXT("Closed")));
	}
	bool ParseDifficulty(const FString& Name, ERPGGameDifficulty& OutDifficulty)
	{
		for (const auto Value : URPGOnlineLobbyWidget::GetDungeonDifficultyOptions())
			if (Same(Name, StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(Value))))
			{ OutDifficulty = Value; return true; }
		return false;
	}
	FString CharacterOption(const FRPGBackendCharacter& Character)
	{
		return Character.Name;
	}
}

FRPGOnlineLobbyPresentation URPGOnlineLobbyWidget::BuildPresentation(bool bLocalLobby, bool bAuthenticated,
	bool bUIRequest, bool bAuthRequest, bool bSteamRequest, bool bFlowRequest, bool bOwnedSelection,
	const FString& SelectedCharacter, const FRPGDungeonSession& Session, ERPGDungeonClientFlowState FlowState,
	const FString& AdmissionBlockReason)
{
	FRPGOnlineLobbyPresentation View;
	View.bBusy = bUIRequest || bAuthRequest || bSteamRequest || bFlowRequest
		|| FlowState == ERPGDungeonClientFlowState::Connecting || FlowState == ERPGDungeonClientFlowState::ReturningToLobby;
	if (!bLocalLobby || View.bBusy) return View;
	View.bCanLogin = !bAuthenticated;
	View.bCanLoadCharacters = bAuthenticated;
	if (!bAuthenticated) return View;
	const bool bTerminal = Same(Session.State, TEXT("Cleared")) || Same(Session.State, TEXT("Failed")) || Same(Session.State, TEXT("Closed"));
	const bool bActive = Active(Session.State) || (!Session.DungeonSessionId.IsEmpty() && !bTerminal);
	const bool bMember = !Session.DungeonSessionId.IsEmpty() && !SelectedCharacter.IsEmpty()
		&& Session.Members.ContainsByPredicate([&](const FRPGDungeonSessionMember& Member)
			{ return Same(Member.CharacterId, SelectedCharacter); });
	View.bCanSelectCharacter = !bActive;
	View.bCanCreateCharacter = !bActive;
	if (!bOwnedSelection || SelectedCharacter.IsEmpty()) return View;
	View.bCanCreateSession = !bActive;
	View.bCanResume = !bActive || !AdmissionBlockReason.IsEmpty();
	View.bCanRefresh = bMember;
	View.bCanLeave = bMember && Same(Session.State, TEXT("Waiting"));
	// Wait for the backend's server-start acknowledgement, not merely an allocator address.
	View.bCanEnter = AdmissionBlockReason.IsEmpty() && bMember && Same(Session.State, TEXT("InProgress"))
		&& !Session.ServerId.TrimStartAndEnd().IsEmpty() && !Session.ServerAddress.TrimStartAndEnd().IsEmpty();
	return View;
}

bool URPGOnlineLobbyWidget::TryNormalizeCharacterName(const FString& Input, FString& NormalizedName)
{
	NormalizedName = Input.TrimStartAndEnd();
	// Match the backend's post-trim UTF-16 length and Unicode control-character checks.
	bool bContainsControl = false;
	for (TCHAR Character : NormalizedName)
		bContainsControl |= Character < 0x20 || (Character >= 0x7f && Character <= 0x9f);
	if (NormalizedName.Len() < 2 || NormalizedName.Len() > 20 || bContainsControl)
	{
		NormalizedName.Reset();
		return false;
	}
	return true;
}

bool URPGOnlineLobbyWidget::IsDevelopmentUIEnabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("RPGDevelopmentLogin"))
		&& FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
#endif
}

TArray<ERPGGameDifficulty> URPGOnlineLobbyWidget::GetDungeonDifficultyOptions()
{
	return {ERPGGameDifficulty::Easy, ERPGGameDifficulty::Normal, ERPGGameDifficulty::Hard, ERPGGameDifficulty::Hell};
}

FString URPGOnlineLobbyWidget::GetDisplayedDungeonDifficulty() const
{
	const auto Session = Flow ? Flow->GetCurrentDungeonSession() : FRPGDungeonSession();
	return Unfinished(Session) ? Session.Difficulty : Difficulty;
}

bool URPGOnlineLobbyWidget::HasUnfinishedDungeonSelection() const
{
	return Flow && Unfinished(Flow->GetCurrentDungeonSession());
}

FString URPGOnlineLobbyWidget::GetDisplayedDungeonId() const
{
	return HasUnfinishedDungeonSelection() ? Flow->GetCurrentDungeonSession().DungeonId : DungeonId;
}

bool URPGOnlineLobbyWidget::GetSelectedDungeonDifficulty(ERPGGameDifficulty& OutDifficulty) const
{
	ERPGGameDifficulty Value;
	if (!ParseDifficulty(GetDisplayedDungeonDifficulty(), Value)
		|| (!HasUnfinishedDungeonSelection() && !GetAvailableDungeonDifficultyOptions().Contains(Value))) return false;
	OutDifficulty = Value;
	return true;
}

bool URPGOnlineLobbyWidget::CanSelectDungeonDifficulty() const
{
	FString SelectedId;
	return CanSelectDungeon() && GetSelectedDungeonId(SelectedId);
}

bool URPGOnlineLobbyWidget::SelectDungeonDifficulty(const ERPGGameDifficulty InDifficulty)
{
	if (!CanSelectDungeonDifficulty() || !GetAvailableDungeonDifficultyOptions().Contains(InDifficulty))
	{ RefreshPresentation(); return false; }
	Difficulty = StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(InDifficulty));
	UIError.Reset();
	RefreshPresentation();
	return true;
}

bool URPGOnlineLobbyWidget::CreateSelectedDungeonSession()
{
	if (!CanSelectDungeonDifficulty()) { RefreshPresentation(); return false; }
	ERPGGameDifficulty Value = ERPGGameDifficulty::Easy;
	FString SelectedId;
	if (!GetSelectedDungeonId(SelectedId) || !GetSelectedDungeonDifficulty(Value))
	{ UIError = TEXT("invalid_dungeon_difficulty"); RefreshPresentation(); return false; }
	UIError.Reset();
	const bool bStarted = Flow->CreateDungeonSession(SelectedId, Difficulty);
	RefreshPresentation();
	return bStarted;
}

void URPGOnlineLobbyWidget::HandleDungeonDifficultySelected(FString Option, ESelectInfo::Type)
{
	if (bUpdatingDifficultyOptions) return;
	ERPGGameDifficulty Value = ERPGGameDifficulty::Easy;
	if (!ParseDifficulty(Option, Value)) { RefreshPresentation(); return; }
	SelectDungeonDifficulty(Value); // rechecks context even for a forced/disabled dispatcher
}

TSharedRef<SWidget> URPGOnlineLobbyWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;
		auto* Border = WidgetTree->ConstructWidget<UBorder>();
		Border->SetBrushColor(FLinearColor(.025f, .04f, .06f, .97f));
		Border->SetPadding(FMargin(18.f));
		auto* PanelSlot = Canvas->AddChildToCanvas(Border);
		PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		PanelSlot->SetOffsets(FMargin(24.f));
		auto* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OnlineLobbyPage"));
		Border->SetContent(Page);
		auto* Header = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OnlineLobbyPinnedHeader"));
		Page->AddChildToVerticalBox(Header);
		TitleText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Header, TEXT("OnlineLobbyTitle"), NSLOCTEXT("RPGOnlineLobby", "GameTitle", "PROJECT RPG"), 26);
		StatusText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Header, TEXT("OnlineLobbyStatus"), FText::GetEmpty());
		NextStepText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Header, TEXT("OnlineLobbyNextStep"), FText::GetEmpty());
		NextStepText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .84f, .48f)));
		auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("OnlineLobbyContentScroll"));
		Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
		Scroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);
		Scroll->SetScrollbarPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto* Sections = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OnlineLobbySections"));
		Scroll->AddChild(Sections);
		auto* LoginWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OnlineLobbyLoginWidth"));
		LoginWidth->SetMaxDesiredWidth(600.f);
		Sections->AddChildToVerticalBox(LoginWidth)->SetHorizontalAlignment(HAlign_Center);
		LoginContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OnlineLobbyLoginContent"));
		LoginWidth->SetContent(LoginContent);
		LobbyContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OnlineLobbyAuthenticatedContent"));
		LobbyContent->SetVisibility(ESlateVisibility::Collapsed);
		auto* Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *LoginContent, TEXT("OnlineLobbyLoginSection"), NSLOCTEXT("RPGOnlineLobby", "LoginSection", "모험을 시작하세요"));
		// Rows changes between sections; these helpers are used synchronously only.
		auto AddText = [this, &Rows](const FName Name, const FText& Label)
		{
			return RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, Name, Label);
		};
		auto AddInput = [this, &Rows](const FName Name, const FText& Hint)
		{
			return RPGOnlineLobbyLayout::Input(*WidgetTree, *Rows, Name, Hint);
		};
		auto AddAction = [this, &Rows](const FName Name, const FText& Label)
		{
			auto* Button = RPGOnlineLobbyLayout::Action(*WidgetTree, *Rows, Name, Label);
			Buttons.Add(Name, Button);
			ActionUnavailableTexts.Add(Name, RPGOnlineLobbyLayout::UnavailableReason(*WidgetTree, *Rows, Name));
			return Button;
		};
		LoginHintText = AddText(TEXT("OnlineLobbyLoginHint"), NSLOCTEXT("RPGOnlineLobby", "LoginHint", "Steam 계정으로 접속한 뒤 캐릭터를 선택하세요. 친구들과 파티를 구성해 던전에 도전할 수 있습니다."));
		AddAction(TEXT("LoginSteamButton"), NSLOCTEXT("RPGOnlineLobby", "SteamStart", "게임 시작"))->SetBackgroundColor(FLinearColor(.08f, .38f, .43f));
		if (IsDevelopmentUIEnabled())
		{
			Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *LoginContent, TEXT("OnlineLobbyDevelopmentLoginSection"), NSLOCTEXT("RPGOnlineLobby", "DevelopmentLoginSection", "개발 전용 로그인"));
			DevelopmentSteamId = AddInput(TEXT("DevelopmentSteamId"), NSLOCTEXT("RPGOnlineLobby", "DevId", "개발 테스트 SteamID (실제 Steam 인증 아님)"));
			AddAction(TEXT("LoginDevelopmentButton"), NSLOCTEXT("RPGOnlineLobby", "DevLogin", "개발 백엔드 로그인"));
		}
		// Keep one set of bound controls alive across authentication expiry and
		// re-login so pending configuration/session work retains its original scope.
		Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Sections, TEXT("OnlineLobbyAccountSection"), NSLOCTEXT("RPGOnlineLobby", "AccountSection", "계정"));
		AccountSection = Rows->GetParent();
		AddAction(TEXT("LogoutBackendButton"), NSLOCTEXT("RPGOnlineLobby", "Logout", "로그아웃"));
		BuildMenuContent(*Sections, true);
		BuildCharacterContent(*Sections);
		Sections->AddChildToVerticalBox(LobbyContent);
		Sections = LobbyContent;
		Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Sections, TEXT("OnlineLobbySelectedCharacterSection"), NSLOCTEXT("RPGOnlineLobby", "SelectedCharacterSection", "플레이 중인 캐릭터"));
		PreparationCharacterText = AddText(TEXT("OnlineLobbyPreparationCharacter"), FText::GetEmpty());
		AddAction(TEXT("ChangeCharacterButton"), NSLOCTEXT("RPGOnlineLobby", "ChangeCharacter", "캐릭터 선택으로 돌아가기"));
		BuildMenuContent(*Sections, false);
		Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Sections, TEXT("OnlineLobbyConfigurationSection"), NSLOCTEXT("RPGOnlineLobby", "ConfigurationSection", "스킬 구성"));
		ConfigurationSection = Rows->GetParent();
		ConfigurationPanel = WidgetTree->ConstructWidget<URPGCharacterConfigurationWidget>(URPGCharacterConfigurationWidget::StaticClass(), TEXT("CharacterConfiguration"));
		ConfigurationPanel->SetOwningPlayer(GetOwningPlayer());
		ConfigurationPanel->SetLobby(this);
		Rows->AddChildToVerticalBox(ConfigurationPanel);
		Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Sections, TEXT("OnlineLobbyEquipmentSection"), NSLOCTEXT("RPGOnlineLobby", "EquipmentSection", "장비 성장 · 제작"));
		EquipmentSection = Rows->GetParent();
		EquipmentPanel = WidgetTree->ConstructWidget<URPGEquipmentGrowthWidget>(URPGEquipmentGrowthWidget::StaticClass(), TEXT("EquipmentGrowth"));
		EquipmentPanel->SetOwningPlayer(GetOwningPlayer());
		EquipmentPanel->SetLobby(this);
		Rows->AddChildToVerticalBox(EquipmentPanel);
		Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Sections, TEXT("OnlineLobbyDungeonSection"), NSLOCTEXT("RPGOnlineLobby", "DungeonSection", "던전 · 파티 준비"));
		DungeonSection = Rows->GetParent();
		SessionText = AddText(TEXT("OnlineLobbySession"), FText::GetEmpty());
		PartyRosterHeader = AddText(TEXT("OnlineLobbyPartyRosterHeader"), FText::GetEmpty());
		PartyRosterText = AddText(TEXT("OnlineLobbyPartyRoster"), FText::GetEmpty());
		PartyRosterHeader->SetVisibility(ESlateVisibility::Collapsed);
		PartyRosterText->SetVisibility(ESlateVisibility::Collapsed);
		PartyRosterText->SetToolTipText(NSLOCTEXT("RPGOnlineLobby", "PartyRosterHint", "서버가 조회한 파티 구성입니다. 이름을 모르는 파티원은 캐릭터 식별자로 표시하며, 접속 상태나 준비 완료를 뜻하지 않습니다."));
		DungeonSelectionText = AddText(TEXT("DungeonSelection"), FText::GetEmpty());
		DungeonsBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("DungeonSelectionOptions"));
		RPGOnlineLobbyLayout::Control(*WidgetTree, *Rows, *DungeonsBox);
		DifficultyBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("DungeonDifficulty"));
		for (const auto Value : GetDungeonDifficultyOptions())
			DifficultyBox->AddOption(StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(Value)));
		RPGOnlineLobbyLayout::Control(*WidgetTree, *Rows, *DifficultyBox);
		AddAction(TEXT("CreateDungeonSessionButton"), NSLOCTEXT("RPGOnlineLobby", "CreateSession", "던전 세션 생성"));
		AddAction(TEXT("ResumeDungeonSessionButton"), NSLOCTEXT("RPGOnlineLobby", "ResumeSession", "진행 중인 세션 복원"));
		AddAction(TEXT("RefreshDungeonSessionButton"), NSLOCTEXT("RPGOnlineLobby", "RefreshSession", "파티 / 서버 준비 새로고침"));
		AddAction(TEXT("EnterDungeonButton"), NSLOCTEXT("RPGOnlineLobby", "EnterDungeon", "준비된 던전 입장"))->SetBackgroundColor(FLinearColor(.08f, .38f, .43f));
		AddAction(TEXT("LeaveDungeonSessionButton"), NSLOCTEXT("RPGOnlineLobby", "LeaveSession", "대기 중인 세션 나가기"));
		AddAction(TEXT("RequestDungeonDepartureButton"), NSLOCTEXT("RPGOnlineLobby", "RequestDeparture", "파티 출발 요청 (방장)"));
		AddAction(TEXT("CancelDungeonDepartureButton"), NSLOCTEXT("RPGOnlineLobby", "CancelDeparture", "출발 요청 취소 (방장)"));
		AddAction(TEXT("RetryDungeonDepartureButton"), NSLOCTEXT("RPGOnlineLobby", "RetryDeparture", "출발 / 취소 결과 재확인"));
		AddAction(TEXT("AbandonDungeonSessionButton"), NSLOCTEXT("RPGOnlineLobby", "AbandonSession", "던전 포기 / 포기 결과 재확인 (개발 테스트)"));
		AbandonmentConfirmation = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DungeonAbandonmentConfirmation"));
		Rows->AddChildToVerticalBox(AbandonmentConfirmation);
		AbandonmentConfirmation->SetVisibility(ESlateVisibility::Collapsed);
		auto* Warning = RPGOnlineLobbyLayout::Text(*WidgetTree, *AbandonmentConfirmation, TEXT("DungeonAbandonmentWarning"), NSLOCTEXT("RPGOnlineLobby", "AbandonmentWarning", "이 캐릭터만 던전을 포기하며 이번 던전 보상에서 제외됩니다. 남은 파티는 계속 진행합니다. 서버가 정산을 시작했다면 포기할 수 없습니다. 응답을 받지 못했을 때는 같은 요청으로 결과를 재확인합니다. 정말 포기하시겠습니까?"));
		Warning->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .74f, .57f)));
		auto AddConfirmationAction = [this](FName Name, FText Label)
		{
			Buttons.Add(Name, RPGOnlineLobbyLayout::Action(*WidgetTree, *AbandonmentConfirmation, Name, Label));
			ActionUnavailableTexts.Add(Name, RPGOnlineLobbyLayout::UnavailableReason(*WidgetTree, *AbandonmentConfirmation, Name));
		};
		AddConfirmationAction(TEXT("ConfirmDungeonAbandonmentButton"), NSLOCTEXT("RPGOnlineLobby", "ConfirmAbandonment", "보상 제외에 동의하고 포기 / 결과 재확인"));
		AddConfirmationAction(TEXT("CancelDungeonAbandonmentButton"), NSLOCTEXT("RPGOnlineLobby", "CancelAbandonment", "취소 — 새 포기 요청을 보내지 않음"));
		Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Sections, TEXT("OnlineLobbySteamSection"), NSLOCTEXT("RPGOnlineLobby", "SteamParty", "Steam 파티"));
		SteamSection = Rows->GetParent();
		AddText(TEXT("SteamPartyLabel"), NSLOCTEXT("RPGOnlineLobby", "SteamPartyHint", "친구 초대와 파티 검색을 이용하세요. 던전 입장은 서버 준비와 멤버십 확인 후 가능합니다."));
		AddAction(TEXT("CreateSteamPartyButton"), NSLOCTEXT("RPGOnlineLobby", "CreateParty", "현재 세션의 Steam 파티 생성"));
		AddAction(TEXT("FindSteamPartiesButton"), NSLOCTEXT("RPGOnlineLobby", "FindParties", "Steam 파티 찾기"));
		PartiesBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("FoundSteamParties"));
		RPGOnlineLobbyLayout::Control(*WidgetTree, *Rows, *PartiesBox);
		AddAction(TEXT("JoinSteamPartyButton"), NSLOCTEXT("RPGOnlineLobby", "JoinParty", "선택한 Steam 파티 참가"));
		AddAction(TEXT("AcceptSteamInviteButton"), NSLOCTEXT("RPGOnlineLobby", "AcceptInvite", "받은 Steam 초대 참가"));
		AddAction(TEXT("InviteSteamFriendsButton"), NSLOCTEXT("RPGOnlineLobby", "InviteFriends", "Steam 친구 초대"));
		AddAction(TEXT("PublishSteamServerButton"), NSLOCTEXT("RPGOnlineLobby", "PublishServer", "준비된 서버 정보를 파티에 게시"));
		AddAction(TEXT("LeaveSteamPartyButton"), NSLOCTEXT("RPGOnlineLobby", "LeaveParty", "Steam 파티 나가기"));
		if (IsDevelopmentUIEnabled())
		{
			Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Sections, TEXT("OnlineLobbyDevelopmentSessionSection"), NSLOCTEXT("RPGOnlineLobby", "DevelopmentSessionSection", "개발 전용 세션 참가"));
			DevelopmentSessionSection = Rows->GetParent();
			DevelopmentSessionId = AddInput(TEXT("DevelopmentSessionId"), NSLOCTEXT("RPGOnlineLobby", "DevSession", "개발용 파티 세션 GUID"));
			AddAction(TEXT("JoinDevelopmentSessionButton"), NSLOCTEXT("RPGOnlineLobby", "DevJoin", "개발용 대기 세션 참가"));
		}
	}
	return Super::RebuildWidget();
}

void URPGOnlineLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RPGConfirmationInput::ConfigureNavigation(Buttons.FindRef(TEXT("ConfirmDungeonAbandonmentButton")),
		Buttons.FindRef(TEXT("CancelDungeonAbandonmentButton")));
	SetIsFocusable(true);
	UGameInstance* GI = GetGameInstance();
	Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
	Auth = GI ? GI->GetSubsystem<URPGSteamAuthSubsystem>() : nullptr;
	Flow = GI ? GI->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	Steam = GI ? GI->GetSubsystem<URPGSteamLobbySubsystem>() : nullptr;
#define BIND_ACTION(Name, Method) if (auto* Button = Buttons.FindRef(TEXT(Name)).Get()) Button->OnClicked.AddUniqueDynamic(this, &ThisClass::Method)
	BIND_ACTION("SelectServerButton", SelectServerPage);
	BIND_ACTION("BackToServersButton", ShowServerPage);
	BIND_ACTION("StartTutorialButton", StartTutorial);
	BIND_ACTION("ReloadTutorialProgressButton", ReloadTutorialProgress);
	BIND_ACTION("OpenSkillMenuButton", OpenSkillMenu);
	BIND_ACTION("OpenEquipmentMenuButton", OpenEquipmentMenu);
	BIND_ACTION("OpenDungeonMenuButton", OpenDungeonMenu);
	BIND_ACTION("CloseLobbyDetailsButton", CloseLobbyDetails);
	BIND_ACTION("LoginSteamButton", LoginSteam);
	BIND_ACTION("LoginDevelopmentButton", LoginDevelopment);
	BIND_ACTION("LogoutBackendButton", LogoutBackend);
	BIND_ACTION("ReloadCharactersButton", ReloadCharacters);
	BIND_ACTION("CreateCharacterButton", CreateCharacter);
	BIND_ACTION("ReloadCreationOptionsButton", ReloadCreationOptions);
	BIND_ACTION("ContinueWithCharacterButton", ContinueWithCharacter);
	BIND_ACTION("ChangeCharacterButton", ChangeCharacter);
	BIND_ACTION("CreateDungeonSessionButton", CreateSession);
	BIND_ACTION("ResumeDungeonSessionButton", ResumeSession);
	BIND_ACTION("RefreshDungeonSessionButton", RefreshSession);
	BIND_ACTION("EnterDungeonButton", EnterDungeon);
	BIND_ACTION("LeaveDungeonSessionButton", LeaveSession);
	BIND_ACTION("RequestDungeonDepartureButton", RequestDeparture);
	BIND_ACTION("CancelDungeonDepartureButton", CancelDeparture);
	BIND_ACTION("RetryDungeonDepartureButton", RetryDeparture);
	BIND_ACTION("AbandonDungeonSessionButton", OpenAbandonmentConfirmation);
	BIND_ACTION("ConfirmDungeonAbandonmentButton", ConfirmAbandonment);
	BIND_ACTION("CancelDungeonAbandonmentButton", CancelAbandonment);
	BIND_ACTION("CreateSteamPartyButton", CreateSteamParty);
	BIND_ACTION("FindSteamPartiesButton", FindSteamParties);
	BIND_ACTION("JoinSteamPartyButton", JoinSteamParty);
	BIND_ACTION("AcceptSteamInviteButton", AcceptSteamInvite);
	BIND_ACTION("InviteSteamFriendsButton", InviteSteamFriends);
	BIND_ACTION("PublishSteamServerButton", PublishSteamServer);
	BIND_ACTION("LeaveSteamPartyButton", LeaveSteamParty);
	BIND_ACTION("JoinDevelopmentSessionButton", JoinDevelopmentSession);
#undef BIND_ACTION
	if (CharactersBox) CharactersBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::SelectCharacter);
	if (CreationClassBox) CreationClassBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleCreationClassSelected);
	if (DifficultyBox) DifficultyBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleDungeonDifficultySelected);
	if (DungeonsBox) DungeonsBox->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleDungeonSelected);
	if (Auth) Auth->OnLoginCompleted.AddUniqueDynamic(this, &ThisClass::HandleLogin);
	if (auto* Lobby = GI ? GI->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr)
		Lobby->OnLogoutCompleted.AddUniqueDynamic(this, &ThisClass::HandleLogout);
	if (Web)
	{
		Web->OnCharactersLoaded.AddUniqueDynamic(this, &ThisClass::HandleCharacters);
		Web->OnCharacterCreated.AddUniqueDynamic(this, &ThisClass::HandleCharacterCreated);
		Web->OnCharacterCreationOptionsLoaded.AddUniqueDynamic(this, &ThisClass::HandleCreationOptions);
	}
	if (Flow)
	{
		Flow->OnFlowStateChanged.AddUniqueDynamic(this, &ThisClass::HandleFlow);
		Flow->OnDungeonSessionChanged.AddUniqueDynamic(this, &ThisClass::HandleSession);
	}
	if (Steam)
	{
		Steam->OnLobbyStateChanged.AddUniqueDynamic(this, &ThisClass::HandleSteamState);
		Steam->OnLobbyChanged.AddUniqueDynamic(this, &ThisClass::HandleSteamLobby);
		Steam->OnPartyInviteAccepted.AddUniqueDynamic(this, &ThisClass::HandleSteamLobby);
		Steam->OnLobbySearchCompleted.AddUniqueDynamic(this, &ThisClass::HandleSteamSearch);
	}
	bPreparationOpen = Flow && !Flow->GetSelectedCharacterId().IsEmpty();
	bServerSelected = bPreparationOpen;
	PollUntil = FPlatformTime::Seconds() + 120.0;
	if (GetWorld()) GetWorld()->GetTimerManager().SetTimer(PollTimer, this, &ThisClass::PollSession, 2.f, true);
	LoadDungeonDefinitions();
	// Arrival may overlap the queued abandonment POST. Retain this initial GET
	// until existing local-lobby/authentication/busy guards allow it.
	bInitialCharacterLoadPending = true;
	RefreshPresentation();
	BeginCharacterLoad();
	BeginCreationOptionsLoad();
}

void URPGOnlineLobbyWidget::NativeDestruct()
{
	bTutorialEntryActive = bTutorialResolvingSession = false;
	TutorialAccount.Reset(); TutorialCharacter.Reset(); TutorialDungeonId.Reset();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(PollTimer);
	for (const auto& Pair : Buttons) if (Pair.Value) Pair.Value->OnClicked.RemoveAll(this);
	if (CharactersBox) CharactersBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::SelectCharacter);
	if (CreationClassBox) CreationClassBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleCreationClassSelected);
	if (DifficultyBox) DifficultyBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleDungeonDifficultySelected);
	if (DungeonsBox) DungeonsBox->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleDungeonSelected);
	if (Auth) Auth->OnLoginCompleted.RemoveDynamic(this, &ThisClass::HandleLogin);
	if (auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr)
		Lobby->OnLogoutCompleted.RemoveDynamic(this, &ThisClass::HandleLogout);
	if (Web)
	{
		Web->OnCharactersLoaded.RemoveDynamic(this, &ThisClass::HandleCharacters);
		Web->OnCharacterCreated.RemoveDynamic(this, &ThisClass::HandleCharacterCreated);
		Web->OnCharacterCreationOptionsLoaded.RemoveDynamic(this, &ThisClass::HandleCreationOptions);
	}
	if (Flow)
	{
		Flow->OnFlowStateChanged.RemoveDynamic(this, &ThisClass::HandleFlow);
		Flow->OnDungeonSessionChanged.RemoveDynamic(this, &ThisClass::HandleSession);
	}
	if (Steam)
	{
		Steam->OnLobbyStateChanged.RemoveDynamic(this, &ThisClass::HandleSteamState);
		Steam->OnLobbyChanged.RemoveDynamic(this, &ThisClass::HandleSteamLobby);
		Steam->OnPartyInviteAccepted.RemoveDynamic(this, &ThisClass::HandleSteamLobby);
		Steam->OnLobbySearchCompleted.RemoveDynamic(this, &ThisClass::HandleSteamSearch);
	}
	Web = nullptr; Auth = nullptr; Flow = nullptr; Steam = nullptr;
	if (EquipmentPanel) EquipmentPanel->SetLobby(nullptr);
	CloseAbandonmentConfirmation();
	bLoadingCharacters = false; bInitialCharacterLoadPending = false;
	bCreatingCharacter = false; bLoggingIn = false; bLoggingOut = false;
	bLoadingCreationOptions = false;
	Super::NativeDestruct();
}

bool URPGOnlineLobbyWidget::HasOwnedSelection() const
{
	return Flow && OwnedCharacters.ContainsByPredicate([this](const FRPGBackendCharacter& Character)
		{ return Same(Character.CharacterId, Flow->GetSelectedCharacterId()); });
}

bool URPGOnlineLobbyWidget::IsFrontendRequestInFlight() const
{
	return bLoadingCharacters || bCreatingCharacter || bLoggingIn || bLoggingOut || bAbandonmentConfirmationOpen
		|| (Web && Web->IsPlayerProgressionRequestInFlight())
		|| (ConfigurationPanel && (ConfigurationPanel->IsRequestInFlight() || ConfigurationPanel->HasUnresolvedSave()))
		|| (EquipmentPanel && (EquipmentPanel->IsBusy() || EquipmentPanel->HasUnresolvedCommand()));
}

bool URPGOnlineLobbyWidget::CanUseCharacterConfiguration(bool bEdit, bool bRetry) const
{
	const auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	if (!Web || !Auth || !Flow || !Steam || !Lobby || !Lobby->IsLobbyController(GetOwningPlayer())
		|| !Web->HasAccessToken() || !HasOwnedSelection() || bLoadingCharacters || bCreatingCharacter || bLoggingIn
		|| bLoggingOut || bAbandonmentConfirmationOpen || Auth->IsLoginRequestInFlight() || Steam->IsLobbyRequestInFlight()
		|| Flow->IsRequestInFlight() || Flow->GetFlowState() == ERPGDungeonClientFlowState::Connecting
		|| Flow->GetFlowState() == ERPGDungeonClientFlowState::ReturningToLobby) return false;
	// Reading/replaying a prior command does not authorize a new configuration change.
	if (bEdit && EquipmentPanel && (EquipmentPanel->IsBusy() || EquipmentPanel->HasUnresolvedCommand())) return false;
	return !bEdit || bRetry || (!HasUnfinishedDungeonSelection() && !Steam->IsInPartyLobby()
		&& !Flow->HasUnresolvedDungeonAbandonment() && !Flow->HasUnresolvedDeparture() && !Flow->HasDepartureRecoveryError());
}

bool URPGOnlineLobbyWidget::CanUseEquipmentGrowth(bool bWrite, bool bRetry) const
{
	bool bAllowed = false;
	GetEquipmentGrowthUnavailableReason(bWrite, bRetry, &bAllowed);
	return bAllowed;
}

FText URPGOnlineLobbyWidget::GetEquipmentGrowthUnavailableReason(bool bWrite, bool bRetry, bool* OutAllowed) const
{
	FRPGEquipmentGrowthAccess Access;
	Access.bHome = bServerSelected && bPreparationOpen;
	Access.bTutorialActive = bTutorialEntryActive;
	Access.bContextReady = CanUseCharacterConfiguration(false, false);
	if (Access.bContextReady)
	{
		Access.bProgressionBusy = Web->IsPlayerProgressionRequestInFlight();
		Access.bConfigurationBusy = ConfigurationPanel && ConfigurationPanel->IsRequestInFlight();
		Access.bConfigurationUnresolved = ConfigurationPanel && ConfigurationPanel->HasUnresolvedSave();
		Access.bActiveSession = HasUnfinishedDungeonSelection();
		Access.bSteamParty = Steam->IsInPartyLobby();
		Access.bTravelUnresolved = Flow->HasUnresolvedDungeonAbandonment() || Flow->HasUnresolvedDeparture() || Flow->HasDepartureRecoveryError();
		Access.bUnsavedConfiguration = ConfigurationPanel && ConfigurationPanel->HasUnsavedChanges();
	}
	const auto Result = RPGEquipmentGrowthUI::EvaluateAccess(Access, bWrite, bRetry);
	if (OutAllowed) *OutAllowed = Result.bAllowed;
	return Result.Reason;
}

FRPGOnlineLobbyPresentation URPGOnlineLobbyWidget::CurrentPresentation() const
{
	const auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	auto View = BuildPresentation(Web && Auth && Flow && Lobby && Lobby->IsLobbyController(GetOwningPlayer()),
		Web && Web->HasAccessToken(), bLoadingCharacters || bCreatingCharacter || bLoggingIn || bLoggingOut || bAbandonmentConfirmationOpen
			|| (Web && Web->IsPlayerProgressionRequestInFlight()) || (ConfigurationPanel && ConfigurationPanel->IsRequestInFlight())
			|| (EquipmentPanel && EquipmentPanel->IsBusy()),
		Auth && Auth->IsLoginRequestInFlight(), Steam && Steam->IsLobbyRequestInFlight(), Flow && Flow->IsRequestInFlight(),
		HasOwnedSelection(), Flow ? Flow->GetSelectedCharacterId() : FString(),
		Flow ? Flow->GetCurrentDungeonSession() : FRPGDungeonSession(),
		Flow ? Flow->GetFlowState() : ERPGDungeonClientFlowState::Idle,
		Flow ? Flow->GetDungeonAdmissionBlockReason() : FString());
	if (ConfigurationPanel && ConfigurationPanel->HasUnresolvedSave())
	{
		View.bCanSelectCharacter = View.bCanCreateCharacter = View.bCanCreateSession = View.bCanResume = false;
		View.bCanLeave = View.bCanEnter = false;
	}
	if (EquipmentPanel && EquipmentPanel->HasUnresolvedCommand())
	{
		// A lease rejection may need session recovery/leave before an exact retry.
		// Keep those existing escape paths, while preventing new travel or identity changes.
		View.bCanSelectCharacter = View.bCanCreateCharacter = View.bCanCreateSession = View.bCanEnter = false;
	}
	if (Flow && Flow->HasUnresolvedDungeonAbandonment())
	{
		View.bCanSelectCharacter = View.bCanCreateCharacter = View.bCanCreateSession = View.bCanResume = false;
		View.bCanRefresh = View.bCanLeave = View.bCanEnter = false;
	}
	if (Flow && (Flow->HasUnresolvedDeparture() || Flow->HasDepartureRecoveryError()))
	{
		View.bCanCreateSession = View.bCanEnter = false;
		if (Flow->HasUnresolvedDeparture()) View.bCanSelectCharacter = false;
	}
	return View;
}

void URPGOnlineLobbyWidget::RefreshPresentation()
{
	if (ConfigurationPanel) ConfigurationPanel->RefreshContext();
	if (EquipmentPanel) EquipmentPanel->RefreshContext();
	if (bAbandonmentConfirmationOpen && !CanConfirmAbandonment()) CloseAbandonmentConfirmation();
	RefreshEntryPresentation();
	const auto View = CurrentPresentation();
	const auto Session = Flow ? Flow->GetCurrentDungeonSession() : FRPGDungeonSession();
	const bool bParty = Steam && Steam->IsInPartyLobby();
	const bool bValidDifficulty = UpdateDungeonSelectionPresentation();
	const bool bReady = View.bCanLoadCharacters && HasOwnedSelection();
	const int32 PartyIndex = PartiesBox ? PartiesBox->GetSelectedIndex() : INDEX_NONE;
	const auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	using Block = ERPGOnlineLobbyBlockReason;
	using Requirement = FRPGOnlineLobbyActionRequirement;
	const Block CommonReason = !Web || !Auth || !Flow || !Lobby || !Lobby->IsLobbyController(GetOwningPlayer())
		? Block::Unavailable : bAbandonmentConfirmationOpen ? Block::ConfirmationOpen
		: View.bBusy || (ConfigurationPanel && ConfigurationPanel->HasUnresolvedSave())
			|| (EquipmentPanel && EquipmentPanel->HasUnresolvedCommand()) ? Block::Busy : Block::None;
	const Requirement Authenticated{Web && Web->HasAccessToken(), Web && !Web->GetAuthenticatedSteamId().IsEmpty()
		? Block::ReauthenticationRequired : Block::LoginRequired};
	const Requirement Owned{HasOwnedSelection(), Block::CharacterRequired};
	const Requirement NoActive{!HasUnfinishedDungeonSelection(), Block::ActiveSession};
	const Requirement Member{Flow && !Session.DungeonSessionId.IsEmpty() && !Flow->GetSelectedCharacterId().IsEmpty()
		&& Session.Members.ContainsByPredicate([this](const FRPGDungeonSessionMember& Entry)
			{ return Same(Entry.CharacterId, Flow->GetSelectedCharacterId()); }), Block::MemberSessionRequired};
	const Requirement Waiting{Same(Session.State, TEXT("Waiting")), Block::WaitingRequired};
	const Requirement Protocol{!Session.LeaderCharacterId.IsEmpty() && Session.LobbyRevision >= 1, Block::LobbyProtocolRequired};
	const Requirement Leader{Flow && Same(Session.LeaderCharacterId, Flow->GetSelectedCharacterId()), Block::LeaderRequired};
	const Requirement NoParty{!bParty, Block::SteamPartyActive};
	const Requirement Party{bParty, Block::SteamPartyRequired};
	const Requirement NoAbandonment{!Flow || !Flow->HasUnresolvedDungeonAbandonment(), Block::AbandonmentUnconfirmed};
	const bool bDepartureUncertain = Flow && Flow->HasUnresolvedDeparture();
	const Requirement NoPendingDeparture{!bDepartureUncertain, Block::DepartureUnconfirmed};
	const Requirement DepartureStorage{!Flow || !Flow->HasDepartureRecoveryError(), Block::DepartureStorageBlocked};
	for (const auto& Pair : Buttons)
		if (Pair.Value)
		{
			Pair.Value->SetIsEnabled(false);
			ActionBlockReasons.Add(Pair.Key, FRPGOnlineLobbyActionAvailability::Explain(false, CommonReason));
		}
	// The original enabled predicates remain the source of truth. Requirements
	// only explain a disabled result and never enable an action or send a request.
	auto Enable = [this, CommonReason](const FName Name, bool bEnabled,
		std::initializer_list<Requirement> Requirements = {})
	{
		if (auto* Button = Buttons.FindRef(Name).Get())
		{
			Button->SetIsEnabled(bEnabled);
			ActionBlockReasons.Add(Name, FRPGOnlineLobbyActionAvailability::Explain(bEnabled, CommonReason, Requirements));
		}
	};
	const bool bSteamEnabled = !FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
	Enable(TEXT("LoginSteamButton"), View.bCanLogin && bSteamEnabled,
		{{!Authenticated.bSatisfied, Block::AlreadyAuthenticated}, {bSteamEnabled, Block::SteamDisabled}});
	Enable(TEXT("LoginDevelopmentButton"), View.bCanLogin && IsDevelopmentUIEnabled(),
		{{IsDevelopmentUIEnabled(), Block::DevelopmentOnly}, {!Authenticated.bSatisfied, Block::AlreadyAuthenticated}});
	Enable(TEXT("LogoutBackendButton"), Lobby && Lobby->CanLogoutFromBackend(GetOwningPlayer()),
		{{Steam != nullptr, Block::Unavailable}, {Web && Web->HasPlayerLogoutCredential(), Block::LogoutCredentialRequired},
		DepartureStorage, NoPendingDeparture, NoAbandonment, NoActive, NoParty});
	Enable(TEXT("ReloadCharactersButton"), View.bCanLoadCharacters, {Authenticated});
	Enable(TEXT("CreateCharacterButton"), View.bCanCreateCharacter && GetSelectedCreationClass(),
		{Authenticated, NoAbandonment, NoActive, {GetSelectedCreationClass() != nullptr, Block::CreationClassRequired}});
	Enable(TEXT("ReloadCreationOptionsButton"), View.bCanCreateCharacter && !bLoadingCreationOptions,
		{Authenticated, NoAbandonment, NoActive});
	Enable(TEXT("ContinueWithCharacterButton"), View.bCanLoadCharacters && HasOwnedSelection() && !bPreparationOpen
		&& bServerSelected && !GetCharacterOption(Flow->GetSelectedCharacterId()).IsEmpty(),
		{Authenticated, Owned});
	const bool bUnsavedConfiguration = ConfigurationPanel && ConfigurationPanel->HasUnsavedChanges();
	Enable(TEXT("ChangeCharacterButton"), bPreparationOpen && View.bCanSelectCharacter && !bParty && !bUnsavedConfiguration && !bTutorialEntryActive,
		{Authenticated, NoAbandonment, NoPendingDeparture, NoActive, NoParty,
		{!bUnsavedConfiguration, Block::UnsavedConfiguration}});
	Enable(TEXT("CreateDungeonSessionButton"), CanSelectDungeonDifficulty() && bValidDifficulty,
		{Authenticated, Owned, NoAbandonment, DepartureStorage, NoPendingDeparture, NoParty, NoActive,
		{bValidDifficulty, Block::DungeonSelectionRequired}});
	Enable(TEXT("ResumeDungeonSessionButton"), View.bCanResume, {Authenticated, Owned, NoAbandonment, NoActive});
	Enable(TEXT("RefreshDungeonSessionButton"), View.bCanRefresh, {Authenticated, Owned, NoAbandonment, Member});
	Enable(TEXT("EnterDungeonButton"), View.bCanEnter,
		{Authenticated, Owned, NoAbandonment, DepartureStorage, NoPendingDeparture, Member,
		{Flow && Flow->GetDungeonAdmissionBlockReason().IsEmpty(), Block::AdmissionBlocked}, {View.bCanEnter, Block::ServerNotReady}});
	Enable(TEXT("LeaveDungeonSessionButton"), View.bCanLeave && !bParty, {Authenticated, Owned, NoAbandonment, Member, NoParty, Waiting});
	const bool bPendingCancel = Flow && Flow->IsPendingDepartureCancellation();
	if (auto* RetryButton = Buttons.FindRef(TEXT("RetryDungeonDepartureButton")).Get())
	{
		if (auto* Label = Cast<UTextBlock>(RetryButton->GetContent()))
			Label->SetText(!bDepartureUncertain ? NSLOCTEXT("RPGOnlineLobby", "RetryDeparture", "출발 / 취소 결과 재확인")
				: bPendingCancel ? NSLOCTEXT("RPGOnlineLobby", "RetryOriginalCancellation", "원래 취소 요청 재확인")
				: NSLOCTEXT("RPGOnlineLobby", "RetryOriginalDeparture", "원래 출발 요청 재확인"));
	}
	Enable(TEXT("RequestDungeonDepartureButton"), View.bCanRefresh && Flow && !bDepartureUncertain && Flow->CanSetDeparture(false),
		{Authenticated, Owned, NoAbandonment, DepartureStorage, NoPendingDeparture, Member, Waiting, Protocol, Leader,
		{Session.DepartureRequestedAt.IsEmpty(), Block::DepartureQueued}});
	Enable(TEXT("CancelDungeonDepartureButton"), View.bCanRefresh && Flow && !bDepartureUncertain && Flow->CanSetDeparture(true),
		{Authenticated, Owned, NoAbandonment, DepartureStorage, NoPendingDeparture, Member, Waiting, Protocol, Leader,
		{!Session.DepartureRequestedAt.IsEmpty(), Block::DepartureNotQueued}});
	Enable(TEXT("RetryDungeonDepartureButton"), View.bCanRefresh && Flow && bDepartureUncertain
		&& (Flow->CanSetDeparture(false) || Flow->CanSetDeparture(true)),
		{Authenticated, Owned, NoAbandonment, {bDepartureUncertain, Block::NoPendingDeparture}, Member, Waiting, Protocol, Leader});
	const bool bAbandonmentTesting = Flow && Flow->IsAbandonmentTestingEnabled();
	if (auto* Button = Buttons.FindRef(TEXT("AbandonDungeonSessionButton")).Get())
	{
		RPGOnlineLobbyLayout::SetControlVisibility(*Button,
			bAbandonmentTesting ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		Enable(TEXT("AbandonDungeonSessionButton"), !View.bBusy && View.bCanLoadCharacters && HasOwnedSelection()
			&& Flow->CanAbandonDungeonSession() && !bParty, {Authenticated, Owned, NoParty, Member});
	}
	if (AbandonmentConfirmation) AbandonmentConfirmation->SetVisibility(bAbandonmentConfirmationOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Enable(TEXT("ConfirmDungeonAbandonmentButton"), bAbandonmentConfirmationOpen && CanConfirmAbandonment());
	Enable(TEXT("CancelDungeonAbandonmentButton"), bAbandonmentConfirmationOpen);
	Enable(TEXT("CreateSteamPartyButton"), bReady && View.bCanLeave && !bParty, {Authenticated, Owned, NoAbandonment, Member, NoParty, Waiting});
	Enable(TEXT("FindSteamPartiesButton"), View.bCanCreateSession && !bParty,
		{Authenticated, Owned, NoAbandonment, DepartureStorage, NoPendingDeparture, NoParty, NoActive});
	Enable(TEXT("JoinSteamPartyButton"), View.bCanCreateSession && !bParty
		&& FoundParties.IsValidIndex(PartyIndex) && FoundParties[PartyIndex].bJoinable,
		{Authenticated, Owned, NoAbandonment, DepartureStorage, NoPendingDeparture, NoParty, NoActive,
		{FoundParties.IsValidIndex(PartyIndex), Block::PartySelectionRequired},
		{FoundParties.IsValidIndex(PartyIndex) && FoundParties[PartyIndex].bJoinable, Block::PartyNotJoinable}});
	Enable(TEXT("AcceptSteamInviteButton"), View.bCanCreateSession && !bParty && Steam && Steam->HasPendingPartyInvite(),
		{Authenticated, Owned, NoAbandonment, DepartureStorage, NoPendingDeparture, NoParty, NoActive,
		{Steam && Steam->HasPendingPartyInvite(), Block::InviteRequired}});
	Enable(TEXT("InviteSteamFriendsButton"), bReady && bParty, {Authenticated, Owned, Party});
	Enable(TEXT("PublishSteamServerButton"), bReady && bParty && View.bCanEnter,
		{Authenticated, Owned, Party, NoAbandonment, DepartureStorage, NoPendingDeparture, Member,
		{Flow && Flow->GetDungeonAdmissionBlockReason().IsEmpty(), Block::AdmissionBlocked}, {View.bCanEnter, Block::ServerNotReady}});
	Enable(TEXT("LeaveSteamPartyButton"), bReady && bParty && (!Flow->HasActiveDungeonSession() || View.bCanLeave),
		{Authenticated, Owned, Party, NoAbandonment, {NoActive.bSatisfied || Waiting.bSatisfied, Block::WaitingRequired}});
	Enable(TEXT("JoinDevelopmentSessionButton"), IsDevelopmentUIEnabled() && View.bCanCreateSession && !bParty,
		{{IsDevelopmentUIEnabled(), Block::DevelopmentOnly}, Authenticated, Owned, NoAbandonment,
		DepartureStorage, NoPendingDeparture, NoParty, NoActive});
	if (CharactersBox) CharactersBox->SetIsEnabled(View.bCanSelectCharacter && !bParty && !bUnsavedConfiguration && !OwnedCharacters.IsEmpty());
	if (CharacterName) CharacterName->SetIsEnabled(View.bCanCreateCharacter);
	if (CreationClassBox) CreationClassBox->SetIsEnabled(View.bCanCreateCharacter && bCreationOptionsReady && !bLoadingCreationOptions && !CreationClasses.IsEmpty());
	if (DevelopmentSteamId)
	{
		const FString PinnedId = Web ? Web->GetAuthenticatedSteamId() : FString();
		if (!PinnedId.IsEmpty()) DevelopmentSteamId->SetText(FText::FromString(PinnedId));
		DevelopmentSteamId->SetIsEnabled(View.bCanLogin && PinnedId.IsEmpty());
	}
	if (DevelopmentSessionId) DevelopmentSessionId->SetIsEnabled(IsDevelopmentUIEnabled() && View.bCanCreateSession && !bParty);
	if (PartiesBox) PartiesBox->SetIsEnabled(View.bCanCreateSession && !bParty);
	RefreshCharacterPresentation();
	FText Status = NSLOCTEXT("RPGOnlineLobby", "LoginNeeded", "로그인 후 캐릭터를 선택하세요.");
	const FString Error = !UIError.IsEmpty() ? UIError : Flow && !Flow->GetLastErrorCode().IsEmpty()
		? Flow->GetLastErrorCode() : Flow && !Flow->GetDungeonAdmissionBlockReason().IsEmpty()
		? Flow->GetDungeonAdmissionBlockReason() : Steam ? Steam->GetLastErrorCode() : FString();
	if (!Web || !Auth || !Flow) Status = NSLOCTEXT("RPGOnlineLobby", "Unavailable", "온라인 서비스가 준비되지 않았습니다.");
	else if (bAbandonmentConfirmationOpen)
		Status = NSLOCTEXT("RPGOnlineLobby", "ConfirmingAbandonment", "아직 포기 요청을 보내지 않았습니다. 아래 내용을 확인한 뒤 포기하거나 취소하세요.");
	else if (bLoggingIn || Auth->IsLoginRequestInFlight())
		Status = NSLOCTEXT("RPGOnlineLobby", "LoginBusy", "계정을 확인하고 있습니다. 잠시 기다려 주세요.");
	else if (View.bBusy) Status = NSLOCTEXT("RPGOnlineLobby", "Busy", "요청 처리 / 이동 중...");
	else if (!Web->HasAccessToken() && !Web->GetAuthenticatedSteamId().IsEmpty())
		Status = NSLOCTEXT("RPGOnlineLobby", "AuthenticationExpired", "인증이 만료되었습니다. 같은 Steam 계정으로 다시 로그인하세요. 캐릭터와 던전 선택은 유지됩니다.");
	else if (ConfigurationPanel && ConfigurationPanel->HasUnresolvedSave())
		Status = NSLOCTEXT("RPGOnlineLobby", "ConfigurationUnconfirmed", "스킬 구성의 저장 결과를 먼저 확인하세요. 같은 저장 요청을 재확인할 수 있습니다.");
	else if (EquipmentPanel && EquipmentPanel->HasUnresolvedCommand())
		Status = NSLOCTEXT("RPGOnlineLobby", "EquipmentUnconfirmed", "장비 성장 · 제작에서 이전 작업의 결과를 먼저 확인하세요. 원래 요청으로 재확인할 수 있습니다.");
	else if (Flow->HasQueuedDungeonAbandonment())
		Status = NSLOCTEXT("RPGOnlineLobby", "AbandonmentQueued", "던전 포기를 확인했습니다. 아직 포기 요청을 보내지 못했습니다. 같은 계정으로 로그인하고 포기 확인 버튼으로 요청하세요. 서버 영수증 전에는 보상 제외가 확정되지 않습니다.");
	else if (Flow->HasUnresolvedDungeonAbandonment())
		Status = NSLOCTEXT("RPGOnlineLobby", "AbandonmentUnconfirmed", "포기 결과를 아직 확인하지 못했습니다. 세션과 캐릭터를 유지하며 같은 포기 요청으로 결과를 재확인하세요. 새 던전 입장은 확인이 끝난 뒤 가능합니다.");
	else if (Flow->HasDepartureRecoveryError())
		Status = NSLOCTEXT("RPGOnlineLobby", "DepartureRecoveryStorageError", "저장된 출발 요청을 복원하거나 정리하지 못했습니다. 저장 공간 / 권한을 확인한 뒤 복원·새로고침 또는 결과 재확인을 다시 시도하세요. 새 출발은 차단됩니다.");
	else if (bDepartureUncertain)
	{
		const FText Intent = bPendingCancel ? NSLOCTEXT("RPGOnlineLobby", "CancellationIntent", "취소")
			: NSLOCTEXT("RPGOnlineLobby", "DepartureIntent", "출발");
		if (Session.DungeonSessionId.IsEmpty())
			Status = FText::Format(NSLOCTEXT("RPGOnlineLobby", "RestoredDepartureNeedsSessionByIntent", "저장된 {0} 요청을 복원했습니다. 진행 중인 세션 복원 버튼으로 서버 상태를 먼저 확인하세요. 새 요청은 만들지 않습니다."), Intent);
		else if (Flow->CanSetDeparture(bPendingCancel))
			Status = FText::Format(NSLOCTEXT("RPGOnlineLobby", "DepartureRetryReadyByIntent", "{0} 요청의 결과가 아직 미확정입니다. 원래 요청 재확인 버튼을 누르면 같은 ID와 버전으로 다시 확인합니다. 새 출발이나 반대 의도 요청은 보내지 않습니다."), Intent);
		else
			Status = FText::Format(NSLOCTEXT("RPGOnlineLobby", "DepartureNeedsRefreshByIntent", "저장된 {0} 요청을 유지하고 있습니다. 세션을 새로고침해 서버 상태를 확인하세요. 재확인이 가능해질 때까지 새 요청은 차단됩니다."), Intent);
	}
	else if (Same(Error, TEXT("dungeon_abandoned")))
		Status = NSLOCTEXT("RPGOnlineLobby", "Abandoned", "서버가 이 캐릭터의 던전 포기를 확인했습니다. 이번 던전 보상에서 제외됐으며 새 던전에 참가할 수 있습니다.");
	else if (Same(Error, TEXT("dungeon_reconnect_grace_expired")))
		Status = NSLOCTEXT("RPGOnlineLobby", "ReconnectGraceExpired", "10분 재접속 유예가 만료되어 이번 던전에 다시 입장할 수 없습니다. 세션 정보를 새로고침하세요. 이미 확정된 보상은 회수되지 않습니다.");
	else if (Same(Error, TEXT("dungeon_member_not_active")))
		Status = NSLOCTEXT("RPGOnlineLobby", "MemberNotActive", "현재 이 캐릭터로 입장할 수 있는 던전이 아닙니다. 세션 정보를 새로고침하세요.");
	else if (Same(Error, TEXT("dungeon_connection_lost")) || Same(Error, TEXT("dungeon_connection_failed"))
		|| Same(Error, TEXT("dungeon_connection_closed")))
		Status = NSLOCTEXT("RPGOnlineLobby", "ConnectionRecovered", "던전 연결을 종료하고 로비로 돌아왔습니다. 세션 정보를 유지했습니다. 보상은 서버 정산을 확인해야 합니다. 같은 계정으로 새로고침하거나 재접속하세요. 던전 포기 처리는 아직 하지 않았습니다.");
	else if (Same(Error, TEXT("backend_logged_out")))
		Status = NSLOCTEXT("RPGOnlineLobby", "LoggedOut", "백엔드에서 로그아웃했습니다. 로그인 후 캐릭터를 다시 선택하세요. Steam 자체의 로그인 계정은 변경하지 않습니다.");
	else if (Same(Error, TEXT("backend_logout_failed")))
		Status = NSLOCTEXT("RPGOnlineLobby", "LogoutFailed", "로그아웃 결과를 확인하지 못했습니다. 계정 정보를 유지했습니다. 다시 시도하세요.");
	else if (Same(Error, TEXT("backend_login_failed")))
		Status = IsDevelopmentUIEnabled()
			? NSLOCTEXT("RPGOnlineLobby", "DevelopmentLoginFailed", "로그인하지 못했습니다. 개발 서버의 실행 상태와 개발 로그인이 허용되어 있는지 확인한 뒤 다시 시도하세요.")
			: NSLOCTEXT("RPGOnlineLobby", "SteamLoginFailed", "로그인하지 못했습니다. Steam 실행 상태와 서버 연결을 확인한 뒤 다시 시도하세요.");
	else if (Same(Error, TEXT("invalid_development_steam_id")))
		Status = NSLOCTEXT("RPGOnlineLobby", "InvalidDevelopmentId", "개발 계정 ID는 17자리 숫자로 입력하세요.");
	else if (Same(Error, TEXT("load_characters_failed")))
		Status = NSLOCTEXT("RPGOnlineLobby", "CharacterListFailed", "캐릭터 목록을 불러오지 못했습니다. 목록 새로고침으로 다시 확인하세요.");
	else if (Same(Error, TEXT("create_character_failed")))
		Status = NSLOCTEXT("RPGOnlineLobby", "CharacterCreationFailed", "캐릭터 생성 결과를 확인하지 못했습니다. 목록을 새로고침해 생성 여부와 이름을 확인하세요.");
	else if (!Error.IsEmpty()) Status = FText::Format(NSLOCTEXT("RPGOnlineLobby", "Error", "요청 실패: {0} — 새로고침 또는 재시도하세요."), FText::FromString(Error));
	else if (Web->HasAccessToken())
	{
		if (OwnedCharacters.IsEmpty()) Status = NSLOCTEXT("RPGOnlineLobby", "NoCharacters", "캐릭터를 생성하거나 목록을 새로고침하세요.");
		else if (!HasOwnedSelection()) Status = NSLOCTEXT("RPGOnlineLobby", "SelectCharacter", "내 캐릭터를 선택하세요.");
		else if (!bPreparationOpen) Status = NSLOCTEXT("RPGOnlineLobby", "ConfirmCharacter", "선택한 캐릭터를 확인한 뒤 접속을 누르세요.");
		else if (View.bCanEnter) Status = NSLOCTEXT("RPGOnlineLobby", "ServerReady", "서버가 준비됐습니다. 던전에 입장할 수 있습니다.");
		else if (Same(Session.State, TEXT("Waiting")))
			Status = Session.LeaderCharacterId.IsEmpty()
				? NSLOCTEXT("RPGOnlineLobby", "LegacyWaiting", "이전 버전의 대기 세션입니다. 나간 뒤 다시 생성하세요.")
				: Session.DepartureRequestedAt.IsEmpty()
					? NSLOCTEXT("RPGOnlineLobby", "Recruiting", "파티 모집 중 — 방장이 출발을 요청할 수 있습니다.")
					: NSLOCTEXT("RPGOnlineLobby", "DepartureQueued", "출발 요청됨 — 서버 배정 전에는 방장이 취소할 수 있습니다.");
		else if (Flow->HasActiveDungeonSession()) Status = NSLOCTEXT("RPGOnlineLobby", "Waiting", "파티 / 서버 준비 대기 중 — 필요하면 새로고침하세요.");
		else Status = NSLOCTEXT("RPGOnlineLobby", "Ready", "새 던전 세션을 만들거나 Steam 파티에 참가하세요.");
	}
	if (StatusText) StatusText->SetText(Status);
	RefreshActionUnavailablePresentation();
	RefreshNextStepPresentation();
	RefreshDungeonPartyPresentation();
	if (SessionText) SessionText->SetText(Session.DungeonSessionId.IsEmpty() ? FText::GetEmpty() :
		FText::Format(NSLOCTEXT("RPGOnlineLobby", "Session", "세션: {0}\n상태: {1} / 인원: {2} / 난이도: {3}"),
			FText::FromString(Session.DungeonSessionId), FText::FromString(Session.State), FText::AsNumber(Session.Members.Num()), FText::FromString(Session.Difficulty)));
	RefreshMenuPresentation();
	if (bAbandonmentConfirmationOpen)
		RPGMenuFocus::Update(*this, {Buttons.FindRef(TEXT("CancelDungeonAbandonmentButton"))});
	else
		RPGMenuFocus::Update(*this, {Buttons.FindRef(TEXT("SelectServerButton")), Buttons.FindRef(TEXT("StartTutorialButton")), DevelopmentSteamId, Buttons.FindRef(TEXT("LoginDevelopmentButton")),
			Buttons.FindRef(TEXT("LoginSteamButton")), Buttons.FindRef(TEXT("ContinueWithCharacterButton")),
			CharactersBox, CreationClassBox, CharacterName, DungeonsBox, DifficultyBox,
			Buttons.FindRef(TEXT("EnterDungeonButton")), Buttons.FindRef(TEXT("CreateDungeonSessionButton")),
			Buttons.FindRef(TEXT("RequestDungeonDepartureButton")), Buttons.FindRef(TEXT("CancelDungeonDepartureButton")),
			Buttons.FindRef(TEXT("RetryDungeonDepartureButton")),
			Buttons.FindRef(TEXT("ResumeDungeonSessionButton")), Buttons.FindRef(TEXT("RefreshDungeonSessionButton")),
			Buttons.FindRef(TEXT("ReloadCharactersButton")), Buttons.FindRef(TEXT("ChangeCharacterButton"))});
	RefreshInputNavigation();
}

void URPGOnlineLobbyWidget::RefreshEntryPresentation()
{
	const bool bAuthenticated = Web && Web->HasAccessToken();
	const ESlateVisibility LoginVisibility = bAuthenticated ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	const bool bShowPreparation = bAuthenticated && bServerSelected && bPreparationOpen
		&& (HasOwnedSelection() || bLoadingCharacters || bInitialCharacterLoadPending);
	const ESlateVisibility CharacterVisibility = bAuthenticated && bServerSelected && !bShowPreparation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const bool bPageChanged = (LoginContent && LoginContent->GetVisibility() != LoginVisibility)
		|| (CharacterContent && CharacterContent->GetVisibility() != CharacterVisibility);
	auto* PC = GetOwningPlayer();
	const bool bRestoreFocus = bPageChanged && PC && (HasUserFocus(PC) || HasUserFocusedDescendants(PC));
	if (LoginContent) LoginContent->SetVisibility(LoginVisibility);
	if (auto* Width = LoginContent ? Cast<USizeBox>(LoginContent->GetParent()) : nullptr)
		Width->SetVisibility(LoginVisibility);
	if (CharacterContent) CharacterContent->SetVisibility(CharacterVisibility);
	if (LobbyContent) LobbyContent->SetVisibility(bShowPreparation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	// Expired credentials must still allow the existing confirmed logout path;
	// hiding the preparation page must not trap the pinned account at login.
	if (AccountSection) AccountSection->SetVisibility(Web && Web->HasPlayerLogoutCredential()
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (auto* SteamButton = Buttons.FindRef(TEXT("LoginSteamButton")).Get())
		RPGOnlineLobbyLayout::SetControlVisibility(*SteamButton,
			IsDevelopmentUIEnabled() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (TitleText) TitleText->SetText(!bAuthenticated ? NSLOCTEXT("RPGOnlineLobby", "GameTitle", "PROJECT RPG")
		: bShowPreparation ? NSLOCTEXT("RPGOnlineLobby", "PreparationTitle", "로비")
		: NSLOCTEXT("RPGOnlineLobby", "CharacterSelectionTitle", "캐릭터 선택"));
	if (LoginHintText)
		LoginHintText->SetText(Web && !Web->GetAuthenticatedSteamId().IsEmpty()
			? NSLOCTEXT("RPGOnlineLobby", "ReentryHint", "접속을 갱신하려면 같은 계정으로 다시 로그인하세요. 선택한 캐릭터와 진행 중인 던전 정보는 유지됩니다.")
			: IsDevelopmentUIEnabled()
				? NSLOCTEXT("RPGOnlineLobby", "DevelopmentEntryHint", "개발 계정으로 접속해 캐릭터와 던전 흐름을 확인할 수 있습니다. 아래에 개발 계정 ID를 입력하세요.")
				: FParse::Param(FCommandLine::Get(), TEXT("nosteam"))
					? NSLOCTEXT("RPGOnlineLobby", "SteamDisabledEntryHint", "이 실행에서는 Steam 접속이 꺼져 있습니다. Steam을 활성화해 게임을 다시 실행하세요.")
				: NSLOCTEXT("RPGOnlineLobby", "LoginHint", "Steam 계정으로 접속한 뒤 캐릭터를 선택하세요. 친구들과 파티를 구성해 던전에 도전할 수 있습니다."));
	// Return focus to this screen only if it already owned focus. The normal
	// candidate update then selects a control on the newly visible page.
	if (bRestoreFocus) SetUserFocus(PC);
}

bool URPGOnlineLobbyWidget::CanUse(FName Name)
{
	RefreshPresentation();
	const UButton* Button = Buttons.FindRef(Name);
	return Button && Button->GetIsEnabled();
}

bool URPGOnlineLobbyWidget::CanConfirmAbandonment() const
{
	return Flow && Web && Auth && HasOwnedSelection() && Flow->CanAbandonDungeonSession()
		&& !bLoadingCharacters && !bCreatingCharacter && !bLoggingIn && !Auth->IsLoginRequestInFlight()
		&& (!Steam || (!Steam->IsLobbyRequestInFlight() && !Steam->IsInPartyLobby()))
		&& Same(ConfirmAbandonmentSessionId, Flow->GetCurrentDungeonSession().DungeonSessionId)
		&& Same(ConfirmAbandonmentCharacterId, Flow->GetSelectedCharacterId())
		&& ConfirmAbandonmentSteamId == Web->GetAuthenticatedSteamId();
}

void URPGOnlineLobbyWidget::CloseAbandonmentConfirmation()
{
	bAbandonmentConfirmationOpen = false;
	ConfirmAbandonmentSessionId.Reset(); ConfirmAbandonmentCharacterId.Reset(); ConfirmAbandonmentSteamId.Reset();
	if (AbandonmentConfirmation) AbandonmentConfirmation->SetVisibility(ESlateVisibility::Collapsed);
}

void URPGOnlineLobbyWidget::OpenAbandonmentConfirmation()
{
	if (!CanUse(TEXT("AbandonDungeonSessionButton"))) return;
	UIError.Reset();
	ConfirmAbandonmentSessionId = Flow->GetCurrentDungeonSession().DungeonSessionId;
	ConfirmAbandonmentCharacterId = Flow->GetSelectedCharacterId();
	ConfirmAbandonmentSteamId = Web->GetAuthenticatedSteamId();
	bAbandonmentConfirmationOpen = true;
	RefreshPresentation();
	RPGConfirmationInput::Focus(*this, Buttons.FindRef(TEXT("CancelDungeonAbandonmentButton")));
}

void URPGOnlineLobbyWidget::CancelAbandonment()
{
	const bool bWasOpen = bAbandonmentConfirmationOpen;
	CloseAbandonmentConfirmation(); // No HTTP and no clearing of a previous uncertain mutation.
	RefreshPresentation();
	if (bWasOpen) RPGConfirmationInput::Focus(*this, Buttons.FindRef(TEXT("AbandonDungeonSessionButton")));
}

FReply URPGOnlineLobbyWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bAbandonmentConfirmationOpen && RPGConfirmationInput::IsBack(InKeyEvent))
	{
		if (!InKeyEvent.IsRepeat()) CancelAbandonment();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply URPGOnlineLobbyWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Bubble only: editable text and open combo lists get the first opportunity
	// to cancel their own interaction. Authored Blueprint handlers keep priority.
	const FReply Reply = Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	if (Reply.IsEventHandled() || !RPGConfirmationInput::IsBack(InKeyEvent)
		|| bAbandonmentConfirmationOpen || !bPreparationOpen || !CharacterContent
		|| !LobbyContent || !LobbyContent->IsVisible()) return Reply;
	// Reuse the button's fresh checks: dirty/uncertain saves, membership, party,
	// authentication and outstanding requests must not be bypassed by a key.
	if (bEquipmentMenuOpen)
	{
		if (!InKeyEvent.IsRepeat()) CloseLobbyDetails();
		return FReply::Handled();
	}
	if (!InKeyEvent.IsRepeat()) ChangeCharacter();
	return FReply::Handled();
}

void URPGOnlineLobbyWidget::ConfirmAbandonment()
{
	if (!bAbandonmentConfirmationOpen || !CanConfirmAbandonment())
	{
		CloseAbandonmentConfirmation(); RefreshPresentation(); return;
	}
	const FString SessionId = ConfirmAbandonmentSessionId;
	const FString CharacterId = ConfirmAbandonmentCharacterId;
	const FString SteamId = ConfirmAbandonmentSteamId;
	CloseAbandonmentConfirmation(); // Re-entrant delegates cannot submit the same click twice.
	Flow->AbandonDungeonSession(GetOwningPlayer(), SessionId, CharacterId, SteamId);
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::PollSession()
{
	AdvanceTutorialEntry();
	if (bInitialCharacterLoadPending) BeginCharacterLoad();
	RefreshPresentation();
	if (bServerSelected && bPreparationOpen && ConfigurationPanel && !bTutorialEntryActive
		&& RPGOnlineLobbyDevelopmentProfile::Resolve(FCommandLine::Get(), IsDevelopmentUIEnabled()).bTraining)
		ConfigurationPanel->EnsureInitialLoad();
	if (Flow && Active(Flow->GetCurrentDungeonSession().State) && CurrentPresentation().bCanRefresh
		&& (bTutorialEntryActive || FPlatformTime::Seconds() < PollUntil)) Flow->RefreshDungeonSession();
}

void URPGOnlineLobbyWidget::BeginCharacterLoad()
{
	if (!Web || !CurrentPresentation().bCanLoadCharacters) return;
	// Consume once before dispatch, including synchronous/re-entrant failure.
	// Empty rosters and failed reads keep the existing explicit Reload action.
	bInitialCharacterLoadPending = false;
	UIError.Reset(); bLoadingCharacters = true;
	RefreshPresentation();
	Web->LoadCharacters();
}

void URPGOnlineLobbyWidget::LoginSteam()
{
	if (!CanUse(TEXT("LoginSteamButton"))) return;
	UIError.Reset(); bLoggingIn = true;
	if (Web->GetAuthenticatedSteamId().IsEmpty())
	{ OwnedCharacters.Reset(); bHasLoadedCharacterList = false; bPreparationOpen = false; }
	RefreshPresentation(); Auth->LoginToBackendWithSteam();
}

void URPGOnlineLobbyWidget::LoginDevelopment()
{
	if (!CanUse(TEXT("LoginDevelopmentButton")) || !DevelopmentSteamId) return;
	const FString Id = DevelopmentSteamId->GetText().ToString().TrimStartAndEnd();
	if (!FRPGBackendPlayerAuthState::IsSteamId(Id)) { UIError = TEXT("invalid_development_steam_id"); RefreshPresentation(); return; }
	UIError.Reset(); bLoggingIn = true;
	if (Web->GetAuthenticatedSteamId().IsEmpty())
	{ OwnedCharacters.Reset(); bHasLoadedCharacterList = false; bPreparationOpen = false; }
	RefreshPresentation(); Auth->LoginToDevelopmentBackend(Id);
}

void URPGOnlineLobbyWidget::LogoutBackend()
{
	if (!CanUse(TEXT("LogoutBackendButton"))) return;
	auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	if (!Lobby) return;
	UIError.Reset();
	// The coordinator checks frontend busy state before accepting the request.
	// Mark the UI pending only after acceptance, including synchronous failure.
	const bool bStarted = Lobby->LogoutFromBackend(GetOwningPlayer());
	bLoggingOut = bStarted && Web && Web->IsPlayerLogoutInFlight();
	if (!bStarted)
	{
		UIError = TEXT("backend_logout_failed");
	}
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::HandleLogout(const bool bSuccess)
{
	bLoggingOut = false;
	if (!bSuccess) { UIError = TEXT("backend_logout_failed"); RefreshPresentation(); return; }
	OwnedCharacters.Reset(); FoundParties.Reset(); PolledSessionId.Reset();
	bServerSelected = bPreparationOpen = bHasLoadedCharacterList = false;
	bCreationOptionsReady = bLoadingCreationOptions = false;
	CreationClasses.Reset();
	if (CreationClassBox) CreationClassBox->ClearOptions();
	bInitialCharacterLoadPending = false;
	bUpdatingOptions = true;
	if (CharactersBox) CharactersBox->ClearOptions();
	if (PartiesBox) PartiesBox->ClearOptions();
	bUpdatingOptions = false;
	if (CharacterName) CharacterName->SetText(FText::GetEmpty());
	if (DevelopmentSteamId) DevelopmentSteamId->SetText(FText::GetEmpty());
	if (DevelopmentSessionId) DevelopmentSessionId->SetText(FText::GetEmpty());
	UIError = TEXT("backend_logged_out");
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::ReloadCharacters() { if (CanUse(TEXT("ReloadCharactersButton"))) BeginCharacterLoad(); }

void URPGOnlineLobbyWidget::CreateCharacter()
{
	if (!CanUse(TEXT("CreateCharacterButton")) || !CharacterName) return;
	FString Name;
	if (!TryNormalizeCharacterName(CharacterName->GetText().ToString(), Name))
	{ UIError = TEXT("invalid_character_name"); RefreshPresentation(); return; }
	const auto* SelectedClass = GetSelectedCreationClass();
	if (!SelectedClass) return;
	const FString ClassId = SelectedClass->ClassId;
	UIError.Reset(); bCreatingCharacter = true;
	RefreshPresentation(); Web->CreateCharacter(Name, ClassId);
}

void URPGOnlineLobbyWidget::SelectCharacter(FString Option, ESelectInfo::Type)
{
	if (bUpdatingOptions) return;
	if (!CurrentPresentation().bCanSelectCharacter || (Steam && Steam->IsInPartyLobby())
		|| (ConfigurationPanel && ConfigurationPanel->HasUnsavedChanges()))
	{ RefreshPresentation(); return; }
	const auto* Character = OwnedCharacters.FindByPredicate([&](const FRPGBackendCharacter& Candidate)
		{ return Same(CharacterOption(Candidate), Option); });
	int32 Matches = 0;
	for (const auto& Candidate : OwnedCharacters) Matches += Same(CharacterOption(Candidate), Option) ? 1 : 0;
	if (!Character || Matches != 1) { UIError = TEXT("character_not_in_owned_roster"); RefreshPresentation(); return; }
	UIError.Reset();
	if (Flow->SelectCharacter(Character->CharacterId))
	{
		bPreparationOpen = false;
		Flow->ResumeDungeonSession();
	}
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::CreateSession()
{
	CreateSelectedDungeonSession();
}

void URPGOnlineLobbyWidget::ResumeSession()
{
	if (!CanUse(TEXT("ResumeDungeonSessionButton"))) return;
	UIError.Reset(); Flow->ResumeDungeonSession(); RefreshPresentation();
}

void URPGOnlineLobbyWidget::RefreshSession()
{
	if (!CanUse(TEXT("RefreshDungeonSessionButton"))) return;
	UIError.Reset(); PollUntil = FPlatformTime::Seconds() + 120.0;
	Flow->RefreshDungeonSession(); RefreshPresentation();
}

void URPGOnlineLobbyWidget::RequestDeparture()
{
	if (!CanUse(TEXT("RequestDungeonDepartureButton"))) return;
	UIError.Reset(); Flow->SetDeparture(false); RefreshPresentation();
}

void URPGOnlineLobbyWidget::CancelDeparture()
{
	if (!CanUse(TEXT("CancelDungeonDepartureButton"))) return;
	UIError.Reset(); Flow->SetDeparture(true); RefreshPresentation();
}

void URPGOnlineLobbyWidget::RetryDeparture()
{
	if (!CanUse(TEXT("RetryDungeonDepartureButton"))) return;
	UIError.Reset(); Flow->RetryDeparture(); RefreshPresentation();
}

void URPGOnlineLobbyWidget::LeaveSession()
{
	if (!CanUse(TEXT("LeaveDungeonSessionButton"))) return;
	UIError.Reset(); Flow->LeaveDungeonSession(); RefreshPresentation();
}

void URPGOnlineLobbyWidget::EnterDungeon()
{
	if (!CanUse(TEXT("EnterDungeonButton"))) return;
	UIError.Reset();
	if (Steam && Steam->IsInPartyLobby()) Steam->ConnectToDungeonServer(GetOwningPlayer());
	else Flow->ConnectToDungeon(GetOwningPlayer(), Flow->GetCurrentDungeonSession().ServerAddress);
	RefreshPresentation();
}

#define STEAM_ACTION(Method, ButtonName, Expression) \
void URPGOnlineLobbyWidget::Method() { if (!CanUse(TEXT(ButtonName)) || !Steam) return; UIError.Reset(); Expression; RefreshPresentation(); }
STEAM_ACTION(CreateSteamParty, "CreateSteamPartyButton", Steam->CreatePartyLobby(4))
STEAM_ACTION(FindSteamParties, "FindSteamPartiesButton", Steam->FindPartyLobbies())
STEAM_ACTION(AcceptSteamInvite, "AcceptSteamInviteButton", Steam->AcceptPendingPartyInvite())
STEAM_ACTION(InviteSteamFriends, "InviteSteamFriendsButton", Steam->ShowPartyInviteOverlay())
STEAM_ACTION(LeaveSteamParty, "LeaveSteamPartyButton", Steam->LeavePartyLobby())
STEAM_ACTION(PublishSteamServer, "PublishSteamServerButton", Steam->PublishDungeonServerAddress())
#undef STEAM_ACTION

void URPGOnlineLobbyWidget::JoinSteamParty()
{
	if (!CanUse(TEXT("JoinSteamPartyButton")) || !Steam || !PartiesBox) return;
	const int32 Index = PartiesBox->GetSelectedIndex();
	if (!FoundParties.IsValidIndex(Index)) return;
	UIError.Reset(); Steam->JoinPartyLobbyByIndex(FoundParties[Index].ResultIndex); RefreshPresentation();
}

void URPGOnlineLobbyWidget::JoinDevelopmentSession()
{
	if (!CanUse(TEXT("JoinDevelopmentSessionButton")) || !DevelopmentSessionId) return;
	const FString Id = DevelopmentSessionId->GetText().ToString().TrimStartAndEnd();
	FGuid Parsed;
	if (!FGuid::Parse(Id, Parsed)) { UIError = TEXT("invalid_dungeon_session_id"); RefreshPresentation(); return; }
	UIError.Reset(); Flow->JoinDungeonSession(Id); RefreshPresentation();
}

void URPGOnlineLobbyWidget::HandleLogin(bool bSuccess)
{
	if (!bLoggingIn) return;
	bLoggingIn = false;
	if (!bSuccess) UIError = TEXT("backend_login_failed");
	else
	{
		BeginCharacterLoad();
		bLoadingCreationOptions = false;
		BeginCreationOptionsLoad();
	}
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::HandleCharacters(const TArray<FRPGBackendCharacter>& Characters, bool bSuccess)
{
	if (!bLoadingCharacters) return;
	bLoadingCharacters = false;
	if (!bSuccess) { UIError = TEXT("load_characters_failed"); RefreshPresentation(); return; }
	OwnedCharacters = Characters;
	bHasLoadedCharacterList = true;
	if (!HasOwnedSelection()) bPreparationOpen = false;
	if (CharactersBox)
	{
		bUpdatingOptions = true;
		CharactersBox->ClearOptions();
		for (const auto& Character : OwnedCharacters) CharactersBox->AddOption(CharacterOption(Character));
		if (HasOwnedSelection()) CharactersBox->SetSelectedOption(GetCharacterOption(Flow->GetSelectedCharacterId()));
		bUpdatingOptions = false;
	}
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::HandleCharacterCreated(const FRPGBackendCharacter&, bool bSuccess)
{
	if (!bCreatingCharacter) return;
	bCreatingCharacter = false;
	if (!bSuccess) UIError = TEXT("create_character_failed");
	else
	{
		if (CharacterName) CharacterName->SetText(FText::GetEmpty());
		BeginCharacterLoad();
	}
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::HandleFlow(ERPGDungeonClientFlowState, const FString&) { RefreshPresentation(); AdvanceTutorialEntry(); }
void URPGOnlineLobbyWidget::HandleSession(const FRPGDungeonSession& Session)
{
	if (!Same(PolledSessionId, Session.DungeonSessionId))
	{
		PolledSessionId = Session.DungeonSessionId;
		PollUntil = FPlatformTime::Seconds() + 120.0;
	}
	RefreshPresentation();
}
void URPGOnlineLobbyWidget::HandleSteamState(ERPGSteamLobbyState, const FString&) { RefreshPresentation(); }
void URPGOnlineLobbyWidget::HandleSteamLobby(const FRPGSteamLobbyInfo&) { RefreshPresentation(); }
void URPGOnlineLobbyWidget::HandleSteamSearch(const TArray<FRPGSteamLobbyInfo>& Results, bool bSuccess)
{
	FoundParties = bSuccess ? Results : TArray<FRPGSteamLobbyInfo>();
	if (PartiesBox)
	{
		PartiesBox->ClearOptions();
		for (const auto& Party : FoundParties)
			PartiesBox->AddOption(FString::Printf(TEXT("%s [%s] %d/%d"), *Party.OwnerName, *Party.LobbyId, Party.CurrentPlayers, Party.MaxPlayers));
	}
	RefreshPresentation();
}

FString URPGOnlineLobbyWidget::GetCharacterOption(const FString& CharacterId) const
{
	const auto* Character = OwnedCharacters.FindByPredicate([&](const FRPGBackendCharacter& Candidate)
		{ return Same(Candidate.CharacterId, CharacterId); });
	if (!Character) return FString();
	int32 Matches = 0;
	for (const auto& Candidate : OwnedCharacters) Matches += Same(Candidate.Name, Character->Name) ? 1 : 0;
	return Matches == 1 ? CharacterOption(*Character) : FString();
}
FText URPGOnlineLobbyWidget::GetDisplayedStatus() const { return StatusText ? StatusText->GetText() : FText::GetEmpty(); }
