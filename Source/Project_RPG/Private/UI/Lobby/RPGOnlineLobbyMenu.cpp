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
#include "UI/Lobby/RPGTradeWidget.h"
#include "Trading/RPGTradeSubsystem.h"
#include "Auction/RPGAuctionSubsystem.h"
#include "UI/Lobby/RPGAuctionWidget.h"
#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "Riding/RPGRidingCollectionSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyLayout.h"
#include "UI/Lobby/RPGTutorialEntryPolicy.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Manager/UIManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

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
	Add(TEXT("OpenEquipmentMenuButton"), NSLOCTEXT("RPGFrontend", "Equipment", "장비 성장 · 제작"));
	Add(TEXT("OpenStoryMenuButton"), NSLOCTEXT("RPGFrontend", "Story", "인물과 이야기"));
	Add(TEXT("OpenTradeMenuButton"), NSLOCTEXT("RPGFrontend", "Trade", "거래 내역"));
	Add(TEXT("OpenAuctionMenuButton"), NSLOCTEXT("RPGFrontend", "Auction", "경매 내역 · 처리 결과"));
	Add(TEXT("OpenRidingCollectionButton"), NSLOCTEXT("RPGFrontend", "RidingCollection", "탈것 보관함"));
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

FString URPGOnlineLobbyWidget::GetPracticeEntryDungeonId() const
{
	const auto Profile = RPGOnlineLobbyDevelopmentProfile::Resolve(FCommandLine::Get(), IsDevelopmentUIEnabled());
	const auto* Progress = ConfigurationPanel ? ConfigurationPanel->GetConfirmedProgression() : nullptr;
	return Profile.EntryDungeonId(Progress ? Progress->Milestones : 0);
}

bool URPGOnlineLobbyWidget::CanStartTutorial() const
{
	const auto Profile = RPGOnlineLobbyDevelopmentProfile::Resolve(FCommandLine::Get(), IsDevelopmentUIEnabled());
	const auto* Lobby = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGOnlineLobbySubsystem>() : nullptr;
	const auto* Progress = ConfigurationPanel ? ConfigurationPanel->GetConfirmedProgression() : nullptr;
	const bool bIntro1 = Profile.bIntro1 && Progress && (Progress->Milestones & 1) != 0;
	return !IsServiceAdmissionBlocked(TEXT("StartTutorialButton")) && Profile.bTraining && Profile.Error.IsEmpty() && (!HasCompletedTutorial() || bIntro1) && Lobby && Lobby->IsLobbyController(GetOwningPlayer())
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

bool URPGOnlineLobbyWidget::CanUseTrading() const
{
	return bServerSelected && bPreparationOpen && !bTutorialEntryActive && CanUseCharacterConfiguration(false, false)
		&& Flow && !Flow->HasUnresolvedDeparture() && !Flow->HasDepartureRecoveryError() && !Flow->HasUnresolvedDungeonAbandonment()
		&& (!ConfigurationPanel || (!ConfigurationPanel->IsRequestInFlight() && !ConfigurationPanel->HasUnresolvedSave() && !ConfigurationPanel->HasUnsavedChanges()))
		&& (!EquipmentPanel || (!EquipmentPanel->IsBusy() && !EquipmentPanel->HasUnresolvedCommand()));
}
void URPGOnlineLobbyWidget::OpenTradeMenu()
{
	if (!CanUseTrading()) return;
	auto* Trades = GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetSubsystem<URPGTradeSubsystem>() : nullptr;
	if (!Trades || !Trades->IsAvailable()) return;
	const auto Owner = Trades->GetTradeOwner();
	if (!Owner) return;
	if (TradePanel && TradePanel->IsInViewport()) { TradePanel->SetKeyboardFocus(); return; }
	TradePanel = CreateWidget<URPGTradeWidget>(GetOwningPlayer(), URPGTradeWidget::StaticClass());
	if (TradePanel)
	{
		TradePanel->OnClosed.AddUObject(this, &ThisClass::HandleTradeClosed);
		TradePanel->SetTradeOwner(Owner.ToSharedRef()); TradePanel->AddToViewport(140);
	}
}
void URPGOnlineLobbyWidget::HandleTradeClosed()
{
	if (CanUseTrading())
		if (auto* Button = Buttons.FindRef(TEXT("OpenTradeMenuButton")).Get(); Button && Button->GetIsEnabled() && Button->IsVisible())
			Button->SetUserFocus(GetOwningPlayer());
}

bool URPGOnlineLobbyWidget::CanOpenAuctionMenu() const
{
	return bServerSelected && bPreparationOpen && !bTutorialEntryActive && CanUseCharacterConfiguration(false,false);
}

bool URPGOnlineLobbyWidget::CanUseRidingCollection() const { return CanUseTrading(); }
void URPGOnlineLobbyWidget::OpenRidingCollection()
{
	if (!CanUseRidingCollection()) return;
	auto* Owner = GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetSubsystem<URPGRidingCollectionSubsystem>() : nullptr;
	const auto Reader = Owner ? Owner->GetReader() : nullptr;
	if (!Reader) return;
	if (RidingCollectionPanel && RidingCollectionPanel->IsInViewport()) { RidingCollectionPanel->SetKeyboardFocus(); return; }
	RidingCollectionPanel = CreateWidget<URPGRidingCollectionWidget>(GetOwningPlayer(), URPGRidingCollectionWidget::StaticClass());
	if (RidingCollectionPanel)
	{
		auto* Panel = RidingCollectionPanel.Get();
		const TWeakObjectPtr<URPGRidingCollectionSubsystem> Weak(Owner);
		Panel->OnClosed.AddUObject(this, &ThisClass::HandleRidingCollectionClosed);
		Panel->SetReadOwner(Reader.ToSharedRef(), [Weak](const FString& Id)
			{ const auto* Subsystem = Weak.Get(); return Subsystem ? Subsystem->DescribeMount(Id) : FText::GetEmpty(); });
		if (RidingCollectionPanel == Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed) Panel->AddToViewport(146);
	}
}
void URPGOnlineLobbyWidget::HandleRidingCollectionClosed()
{
	if (auto* Owner = GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetSubsystem<URPGRidingCollectionSubsystem>() : nullptr) Owner->ReleaseReader();
	RidingCollectionPanel = nullptr;
	if (CanUseRidingCollection())
		if (auto* Button = Buttons.FindRef(TEXT("OpenRidingCollectionButton")).Get(); Button && Button->GetIsEnabled() && Button->IsVisible())
			Button->SetUserFocus(GetOwningPlayer());
}
void URPGOnlineLobbyWidget::OpenAuctionMenu()
{
	if(!CanOpenAuctionMenu())return;
	auto* Subsystem=GetOwningLocalPlayer()?GetOwningLocalPlayer()->GetSubsystem<URPGAuctionSubsystem>():nullptr;
	const auto Owner=Subsystem?Subsystem->GetCoordinator():nullptr;
	if(!Owner || !Owner->GetScreenView().bAvailable)return;
	if(AuctionPanel && AuctionPanel->IsInViewport()){AuctionPanel->SetKeyboardFocus();return;}
	AuctionPanel=CreateWidget<URPGAuctionWidget>(GetOwningPlayer(),URPGAuctionWidget::StaticClass());
	if(AuctionPanel){AuctionPanel->OnClosed.AddUObject(this,&ThisClass::HandleAuctionClosed);AuctionPanel->SetOwner(Owner.ToSharedRef());AuctionPanel->AddToViewport(145);}
}
void URPGOnlineLobbyWidget::HandleAuctionClosed()
{
	if(CanOpenAuctionMenu())if(auto* Button=Buttons.FindRef(TEXT("OpenAuctionMenuButton")).Get();Button && Button->GetIsEnabled() && Button->IsVisible())
		Button->SetUserFocus(GetOwningPlayer());
}

bool URPGOnlineLobbyWidget::CanOpenNarrativeDialogues() const
{
	return bServerSelected && bPreparationOpen && !bTutorialEntryActive
		&& CanUseCharacterConfiguration(false, false) && !HasUnfinishedDungeonSelection()
		&& Flow && !Flow->HasUnresolvedDeparture() && !Flow->HasDepartureRecoveryError()
		&& !Flow->HasUnresolvedDungeonAbandonment()
		&& (!ConfigurationPanel || (!ConfigurationPanel->IsRequestInFlight() && !ConfigurationPanel->HasUnresolvedSave()))
		&& (!EquipmentPanel || (!EquipmentPanel->IsBusy() && !EquipmentPanel->HasUnresolvedCommand()));
}
void URPGOnlineLobbyWidget::OpenStoryMenu() { OpenNarrativeDialogues(); }
bool URPGOnlineLobbyWidget::OpenNarrativeDialogues()
{
	if (!CanOpenNarrativeDialogues()) return false;
	if (NarrativePanel && NarrativePanel->IsInViewport()) return true;
	FGuid Character;
	if (!FGuid::Parse(Flow->GetSelectedCharacterId(), Character) || !Character.IsValid()) return false;
	auto* Panel = CreateWidget<URPGNarrativeDialogueWidget>(GetOwningPlayer(), URPGNarrativeDialogueWidget::StaticClass());
	if (!Panel || !Panel->OpenForCharacter(Character)) return false;
	NarrativePanel = Panel;
	UUIManager::Get(GetOwningLocalPlayer())->PushUI(Panel);
	if (!Panel->IsInViewport()) { NarrativePanel = nullptr; return false; }
	return true;
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
	TutorialDungeonId = GetPracticeEntryDungeonId();
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
	TutorialAccount.Reset(); TutorialCharacter.Reset(); TutorialDungeonId.Reset();
	TutorialEntryFailure = NSLOCTEXT("RPGFrontend", "PracticeFailed", "실습 입장을 준비하지 못했습니다. 연결을 확인한 뒤 다시 시도해 주세요. 기존 진행 정보는 유지됩니다.");
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
		Flow->IsRequestInFlight(), Flow->HasUnresolvedDeparture(), !Flow->GetDungeonAdmissionBlockReason().IsEmpty(), TutorialDungeonId);
	bool bStarted = true;
	switch (Decision)
	{
	case ERPGTutorialEntryAction::Create:
		bStarted = Flow->CreateDungeonSession(TutorialDungeonId, TEXT("Normal")); break;
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
	// This changes only the frontend page for the same configured endpoint. Keep recovery
	// reachable after widget recreation even when an existing trade pins gameplay actions.
	Control(TEXT("SelectServerButton"), bAuth && !bLoggingOut, bAuth && !bServerSelected);
	Control(TEXT("BackToServersButton"), bAuth && CurrentPresentation().bCanSelectCharacter && !bTutorialEntryActive
		&& (!Flow || !Flow->HasUnresolvedDeparture()) && (!Steam || !Steam->IsInPartyLobby()), bAuth && bServerSelected && !bPreparationOpen);
	Control(TEXT("StartTutorialButton"), CanStartTutorial() && !bTutorialEntryActive, bHome && Profile.bTraining);
	const auto* Progress = ConfigurationPanel ? ConfigurationPanel->GetConfirmedProgression() : nullptr;
	const bool bIntro1Entry = Profile.bIntro1 && Progress && (Progress->Milestones & 1) != 0;
	Control(TEXT("ReloadTutorialProgressButton"), bHome && !bTutorialEntryActive && ConfigurationPanel
		&& !ConfigurationPanel->IsRequestInFlight() && !ConfigurationPanel->HasUnsavedChanges()
		&& !ConfigurationPanel->HasUnresolvedSave() && CanUseCharacterConfiguration(false, false), bHome && Profile.bTraining && !Progress);
	if (auto* Button = Buttons.FindRef(TEXT("StartTutorialButton")).Get())
		if (auto* Label = Cast<UTextBlock>(Button->GetContent()))
			Label->SetText(bIntro1Entry ? NSLOCTEXT("RPGFrontend", "Intro1Entry", "입문 1 입장")
				: HasCompletedTutorial() ? NSLOCTEXT("RPGFrontend", "TutorialDone", "튜토리얼 완료")
				: NSLOCTEXT("RPGFrontend", "Tutorial", "튜토리얼 입장"));
	Control(TEXT("OpenSkillMenuButton"), bHome && !bTutorialEntryActive, bHome);
	Control(TEXT("OpenEquipmentMenuButton"), bHome && CanUseEquipmentGrowth(false, false), bHome);
	Control(TEXT("OpenStoryMenuButton"), CanOpenNarrativeDialogues(), bHome);
	auto* Trades = GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetSubsystem<URPGTradeSubsystem>() : nullptr;
	Control(TEXT("OpenTradeMenuButton"), CanUseTrading(), bHome && Trades && Trades->IsAvailable());
	auto* Auctions=GetOwningLocalPlayer()?GetOwningLocalPlayer()->GetSubsystem<URPGAuctionSubsystem>():nullptr;
	const auto AuctionOwner=Auctions?Auctions->GetCoordinator():nullptr;
	Control(TEXT("OpenAuctionMenuButton"),CanOpenAuctionMenu(),bHome && AuctionOwner && AuctionOwner->GetScreenView().bAvailable);
	Control(TEXT("OpenRidingCollectionButton"), CanUseRidingCollection(), bHome);
	if (RidingCollectionPanel && !CanUseRidingCollection()) RidingCollectionPanel->CloseScreen();
	if (NarrativePanel && NarrativePanel->IsInViewport()
		&& (!CanOpenNarrativeDialogues() || NarrativePanel->GetCharacterId().ToString(EGuidFormats::DigitsWithHyphensLower) != Flow->GetSelectedCharacterId()))
		NarrativePanel->CloseScreen();
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
			: bIntro1Entry ? NSLOCTEXT("RPGFrontend", "Intro1Ready", "접근 실습에 입장할 수 있습니다. 새 스킬은 스킬 구성에서 편성·저장해 주세요.")
			: HasCompletedTutorial() ? NSLOCTEXT("RPGFrontend", "TrainingFinished", "튜토리얼을 완료했습니다. 로비에서 캐릭터와 스킬을 확인해 보세요.")
			: !Progress ? NSLOCTEXT("RPGFrontend", "CheckProgress", "캐릭터의 진행 정보를 확인하고 있습니다. 연결을 확인하거나 진행 정보를 다시 읽어 주세요.")
			: NSLOCTEXT("RPGFrontend", "LobbyReady", "캐릭터 준비가 끝났습니다. 튜토리얼을 시작해 보세요."));
		if (NextStepText) NextStepText->SetText(bTutorialEntryActive ? FText::GetEmpty()
			: bIntro1Entry ? NSLOCTEXT("RPGFrontend", "BeforeIntro1", "다음 단계: 회전 베기를 편성·저장하고 입문 1 입장을 누르세요.")
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
