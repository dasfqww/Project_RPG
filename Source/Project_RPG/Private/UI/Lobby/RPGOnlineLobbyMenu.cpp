#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyDevelopmentProfile.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"
#include "UI/Lobby/RPGEquipmentGrowthWidget.h"
#include "UI/Lobby/RPGOnlineLobbyLayout.h"
#include "UI/Lobby/RPGTutorialEntryPolicy.h"

void URPGOnlineLobbyWidget::BuildMenuContent(UVerticalBox& Parent, bool bServerPage)
{
	auto& Target = bServerPage ? ServerContent : MenuContent;
	Target = WidgetTree->ConstructWidget<UVerticalBox>();
	Parent.AddChildToVerticalBox(Target);
	auto* Rows = RPGOnlineLobbyLayout::Section(*WidgetTree, *Target,
		bServerPage ? TEXT("ServerSelectionSection") : TEXT("LobbyMenuSection"),
		bServerPage ? NSLOCTEXT("RPGFrontend", "ChooseServer", "플레이할 서버를 선택하세요")
		: NSLOCTEXT("RPGFrontend", "Adventure", "모험"));
	auto Add = [&](FName Name, const FText& Text)
	{
		auto* Button = RPGOnlineLobbyLayout::Action(*WidgetTree, *Rows, Name, Text);
		Buttons.Add(Name, Button); return Button;
	};
	if (bServerPage)
	{
		// Exactly the configured authenticated backend, never an invented region
		// list or a selectable address with no real routing/character scope.
		RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("CurrentServerName"),
			IsDevelopmentUIEnabled() ? NSLOCTEXT("RPGFrontend", "DevelopmentServer", "개발 서버")
			: NSLOCTEXT("RPGFrontend", "CurrentServer", "게임 서버"));
		Add(TEXT("SelectServerButton"), NSLOCTEXT("RPGFrontend", "SelectServer", "서버 선택"));
		return;
	}
	Add(TEXT("StartTutorialButton"), NSLOCTEXT("RPGFrontend", "Tutorial", "튜토리얼 입장"))->SetBackgroundColor(FLinearColor(.08f, .38f, .43f));
	Add(TEXT("ReloadTutorialProgressButton"), NSLOCTEXT("RPGFrontend", "ReloadProgress", "진행 정보 다시 확인"));
	Add(TEXT("OpenDungeonMenuButton"), NSLOCTEXT("RPGFrontend", "Dungeons", "던전 · 파티"));
	Add(TEXT("OpenSkillMenuButton"), NSLOCTEXT("RPGFrontend", "Skills", "스킬 구성"));
	Add(TEXT("OpenEquipmentMenuButton"), NSLOCTEXT("RPGFrontend", "Equipment", "장비 강화 · 이전"));
	Add(TEXT("CloseLobbyDetailsButton"), NSLOCTEXT("RPGFrontend", "MenuHome", "로비 메뉴로 돌아가기"));
	TutorialStatusText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Rows, TEXT("TutorialEntryStatus"), FText::GetEmpty());
}

bool URPGOnlineLobbyWidget::HasCompletedTutorial() const
{
	if (!Flow || !HasOwnedSelection()) return false;
	if (const auto* Saved = ConfigurationPanel ? ConfigurationPanel->GetConfirmedProgression() : nullptr)
		if ((Saved->Milestones & 1) != 0) return true;
	const auto Session = Flow->GetCurrentDungeonSession();
	return Session.DungeonId.Equals(TEXT("Progression.Training"), ESearchCase::CaseSensitive)
		&& Session.State.Equals(TEXT("Cleared"), ESearchCase::CaseSensitive)
		&& Session.Members.ContainsByPredicate([&](const FRPGDungeonSessionMember& Member)
			{ return Member.CharacterId == Flow->GetSelectedCharacterId(); });
}

bool URPGOnlineLobbyWidget::CanStartTutorial() const
{
	const auto Profile = RPGOnlineLobbyDevelopmentProfile::Resolve(FCommandLine::Get(), IsDevelopmentUIEnabled());
	const auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	return Profile.bTraining && Profile.Error.IsEmpty() && !HasCompletedTutorial() && Lobby && Lobby->IsLobbyController(GetOwningPlayer())
		&& ConfigurationPanel && ConfigurationPanel->GetConfirmedProgression()
		&& bServerSelected && bPreparationOpen && Web && Web->HasAccessToken() && !Web->GetAuthenticatedSteamId().IsEmpty()
		&& Flow && HasOwnedSelection() && !Flow->IsRequestInFlight() && !Flow->HasUnresolvedDungeonAbandonment()
		&& !Flow->HasDepartureRecoveryError() && !IsFrontendRequestInFlight()
		&& (!Steam || (!Steam->IsInPartyLobby() && !Steam->IsLobbyRequestInFlight()))
		&& (!ConfigurationPanel || !ConfigurationPanel->HasUnsavedChanges());
}

void URPGOnlineLobbyWidget::SelectServerPage()
{
	if (!CanUse(TEXT("SelectServerButton"))) return;
	bServerSelected = true; UIError.Reset(); RefreshPresentation();
}

void URPGOnlineLobbyWidget::ShowServerPage()
{
	if (!CanUse(TEXT("BackToServersButton"))) return;
	bServerSelected = bPreparationOpen = false; UIError.Reset(); RefreshPresentation();
}

void URPGOnlineLobbyWidget::OpenSkillMenu()
{
	if (!CanUse(TEXT("OpenSkillMenuButton"))) return;
	bSkillMenuOpen = true; bDungeonMenuOpen = bEquipmentMenuOpen = false; RefreshPresentation();
	if (ConfigurationPanel && RPGOnlineLobbyDevelopmentProfile::Resolve(FCommandLine::Get(), IsDevelopmentUIEnabled()).bTraining)
		ConfigurationPanel->EnsureInitialLoad();
}

void URPGOnlineLobbyWidget::ReloadTutorialProgress()
{
	if (!CanUse(TEXT("ReloadTutorialProgressButton")) || !ConfigurationPanel
		|| ConfigurationPanel->HasUnsavedChanges() || ConfigurationPanel->HasUnresolvedSave()) return;
	ConfigurationPanel->LoadSavedConfiguration();
}

void URPGOnlineLobbyWidget::OpenEquipmentMenu()
{
	if (!CanUse(TEXT("OpenEquipmentMenuButton"))) return;
	bEquipmentMenuOpen = true; bSkillMenuOpen = bDungeonMenuOpen = false;
	RefreshPresentation();
	if (EquipmentPanel) EquipmentPanel->OpenScreen();
}

void URPGOnlineLobbyWidget::OpenDungeonMenu()
{
	if (!CanUse(TEXT("OpenDungeonMenuButton"))) return;
	bDungeonMenuOpen = true; bSkillMenuOpen = bEquipmentMenuOpen = false; RefreshPresentation();
}

void URPGOnlineLobbyWidget::CloseLobbyDetails()
{
	if (!CanUse(TEXT("CloseLobbyDetailsButton"))) return;
	bSkillMenuOpen = bDungeonMenuOpen = bEquipmentMenuOpen = false; RefreshPresentation();
}

void URPGOnlineLobbyWidget::StartTutorial()
{
	if (!CanStartTutorial() || bTutorialEntryActive) return;
	UIError.Reset(); TutorialEntryFailure = FText::GetEmpty();
	bSkillMenuOpen = bDungeonMenuOpen = bEquipmentMenuOpen = false;
	TutorialAccount = Web->GetAuthenticatedSteamId(); TutorialCharacter = Flow->GetSelectedCharacterId();
	bTutorialEntryActive = bTutorialResolvingSession = true;
	TutorialDeadline = FPlatformTime::Seconds() + 150.0;
	// Retry only an existing captured command after an explicit user action.
	// Otherwise resolve this character's real membership before creating anything.
	const bool bStarted = Flow->HasUnresolvedDeparture() ? Flow->RetryDeparture()
		: Flow->HasActiveDungeonSession() ? Flow->RefreshDungeonSession() : Flow->ResumeDungeonSession();
	if (!bStarted) StopTutorialEntry();
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::StopTutorialEntry()
{
	bTutorialEntryActive = bTutorialResolvingSession = false;
	TutorialAccount.Reset(); TutorialCharacter.Reset();
	TutorialEntryFailure = NSLOCTEXT("RPGFrontend", "TutorialFailed", "튜토리얼 입장을 준비하지 못했습니다. 연결을 확인한 뒤 다시 시도해 주세요. 기존 진행 정보는 유지됩니다.");
}

void URPGOnlineLobbyWidget::AdvanceTutorialEntry()
{
	if (!bTutorialEntryActive || bAdvancingTutorial) return;
	TGuardValue<bool> Guard(bAdvancingTutorial, true);
	if (!Web || !Flow || !Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != TutorialAccount
		|| Flow->GetSelectedCharacterId() != TutorialCharacter || !HasOwnedSelection()
		|| FPlatformTime::Seconds() > TutorialDeadline || Flow->GetFlowState() == ERPGDungeonClientFlowState::Error
		|| !Flow->GetLastErrorCode().IsEmpty())
	{ StopTutorialEntry(); RefreshPresentation(); return; }
	if (Flow->IsRequestInFlight()) return;
	const auto State = Flow->GetFlowState();
	if (State != ERPGDungeonClientFlowState::Idle && State != ERPGDungeonClientFlowState::InSession) return;
	bTutorialResolvingSession = false;
	const auto Decision = FRPGTutorialEntryPolicy::Decide(Flow->GetCurrentDungeonSession(), TutorialCharacter,
		Flow->IsRequestInFlight(), Flow->HasUnresolvedDeparture(), !Flow->GetDungeonAdmissionBlockReason().IsEmpty());
	bool bStarted = true;
	switch (Decision)
	{
	case ERPGTutorialEntryAction::Create:
		bStarted = Flow->CreateDungeonSession(TEXT("Progression.Training"), TEXT("Normal")); break;
	case ERPGTutorialEntryAction::Depart:
		bStarted = Flow->CanSetDeparture(false) && Flow->SetDeparture(false); break;
	case ERPGTutorialEntryAction::Connect:
		bStarted = CurrentPresentation().bCanEnter && Flow->ConnectToDungeon(GetOwningPlayer(), Flow->GetCurrentDungeonSession().ServerAddress); break;
	case ERPGTutorialEntryAction::Reject:
		bStarted = false; break;
	case ERPGTutorialEntryAction::Wait: break;
	}
	if (!bStarted) StopTutorialEntry();
	RefreshPresentation();
}

void URPGOnlineLobbyWidget::RefreshMenuPresentation()
{
	const bool bAuth = Web && Web->HasAccessToken();
	const bool bHome = bAuth && bServerSelected && bPreparationOpen;
	const auto SetVisible = [](UWidget* Widget, bool bVisible) { if (Widget) Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); };
	SetVisible(ServerContent, bAuth && !bServerSelected);
	SetVisible(MenuContent, bHome);
	SetVisible(ConfigurationSection, bHome && bSkillMenuOpen);
	SetVisible(EquipmentSection, bHome && bEquipmentMenuOpen);
	if (EquipmentPanel) EquipmentPanel->SetScreenOpen(bHome && bEquipmentMenuOpen);
	SetVisible(DungeonSection, bHome && bDungeonMenuOpen);
	SetVisible(SteamSection, bHome && bDungeonMenuOpen);
	SetVisible(DevelopmentSessionSection, bHome && bDungeonMenuOpen && FParse::Param(FCommandLine::Get(), TEXT("RPGAdvancedLobby")));
	const auto Profile = RPGOnlineLobbyDevelopmentProfile::Resolve(FCommandLine::Get(), IsDevelopmentUIEnabled());
	auto Control = [&](FName Name, bool bEnabled, bool bVisible)
	{
		if (auto* Button = Buttons.FindRef(Name).Get()) { Button->SetIsEnabled(bEnabled); RPGOnlineLobbyLayout::SetControlVisibility(*Button, bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	};
	Control(TEXT("SelectServerButton"), bAuth && !IsFrontendRequestInFlight(), bAuth && !bServerSelected);
	Control(TEXT("BackToServersButton"), bAuth && CurrentPresentation().bCanSelectCharacter && !bTutorialEntryActive
		&& (!Flow || !Flow->HasUnresolvedDeparture()) && (!Steam || !Steam->IsInPartyLobby()), bAuth && bServerSelected && !bPreparationOpen);
	Control(TEXT("StartTutorialButton"), CanStartTutorial() && !bTutorialEntryActive, bHome && Profile.bTraining);
	const auto* Progress = ConfigurationPanel ? ConfigurationPanel->GetConfirmedProgression() : nullptr;
	Control(TEXT("ReloadTutorialProgressButton"), bHome && !bTutorialEntryActive && ConfigurationPanel
		&& !ConfigurationPanel->IsRequestInFlight() && !ConfigurationPanel->HasUnsavedChanges()
		&& !ConfigurationPanel->HasUnresolvedSave() && CanUseCharacterConfiguration(false, false), bHome && Profile.bTraining && !Progress);
	if (auto* Button = Buttons.FindRef(TEXT("StartTutorialButton")).Get())
		if (auto* Label = Cast<UTextBlock>(Button->GetContent()))
			Label->SetText(HasCompletedTutorial() ? NSLOCTEXT("RPGFrontend", "TutorialDone", "튜토리얼 완료")
				: NSLOCTEXT("RPGFrontend", "Tutorial", "튜토리얼 입장"));
	Control(TEXT("OpenSkillMenuButton"), bHome && !bTutorialEntryActive, bHome);
	Control(TEXT("OpenEquipmentMenuButton"), bHome && CanUseEquipmentGrowth(false, false), bHome);
	Control(TEXT("OpenDungeonMenuButton"), bHome && !bTutorialEntryActive, bHome && !Profile.bTraining);
	Control(TEXT("CloseLobbyDetailsButton"), bHome && !bTutorialEntryActive, bHome && (bSkillMenuOpen || bDungeonMenuOpen || bEquipmentMenuOpen));
	if (TutorialStatusText)
	{
		TutorialStatusText->SetText(bTutorialEntryActive ? NSLOCTEXT("RPGFrontend", "TutorialPreparing", "튜토리얼을 준비하고 있습니다. 잠시 기다려 주세요.") : TutorialEntryFailure);
		SetVisible(TutorialStatusText, bTutorialEntryActive || !TutorialEntryFailure.IsEmpty());
	}
	if (bHome && Profile.bTraining)
	{
		if (StatusText) StatusText->SetText(bTutorialEntryActive
			? NSLOCTEXT("RPGFrontend", "Preparing", "튜토리얼 입장 준비 중")
			: !TutorialEntryFailure.IsEmpty() ? TutorialEntryFailure
			: HasCompletedTutorial() ? NSLOCTEXT("RPGFrontend", "TrainingFinished", "튜토리얼을 완료했습니다. 로비에서 캐릭터와 스킬을 확인해 보세요.")
			: !Progress ? NSLOCTEXT("RPGFrontend", "CheckProgress", "캐릭터의 진행 정보를 확인하고 있습니다. 연결을 확인하거나 진행 정보를 다시 읽어 주세요.")
			: NSLOCTEXT("RPGFrontend", "LobbyReady", "캐릭터 준비가 끝났습니다. 튜토리얼을 시작해 보세요."));
		if (NextStepText) NextStepText->SetText(bTutorialEntryActive ? FText::GetEmpty()
			: HasCompletedTutorial() ? NSLOCTEXT("RPGFrontend", "AfterTutorial", "다음 단계: 스킬 구성에서 새로 얻은 SP와 습득한 스킬을 확인하세요.")
			: !Progress ? NSLOCTEXT("RPGFrontend", "ProgressRequired", "진행 정보가 확인되면 튜토리얼에 입장할 수 있습니다.")
			: NSLOCTEXT("RPGFrontend", "TutorialNext", "튜토리얼 입장을 누르면 자동으로 이동합니다."));
	}
	if (bAuth && !bServerSelected)
	{
		if (TitleText) TitleText->SetText(NSLOCTEXT("RPGFrontend", "Servers", "서버 선택"));
		if (StatusText) StatusText->SetText(NSLOCTEXT("RPGFrontend", "ServerPrompt", "플레이할 서버를 선택하세요."));
		if (NextStepText) NextStepText->SetText(FText::GetEmpty());
	}
}
