#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyPolicyTest, "ProjectRPG.Online.DungeonLobbyPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGOnlineLobbyPolicyTest::RunTest(const FString&)
{
	using Widget = URPGOnlineLobbyWidget;
	FRPGDungeonSession Session;
	FRPGDungeonSessionMember Member;
	Member.CharacterId = TEXT("owned-character");
	Session.DungeonSessionId = TEXT("assigned-session");
	Session.ServerId = TEXT("assigned-server");
	Session.ServerAddress = TEXT("127.0.0.1:18827");
	Session.Members.Add(Member);
	auto View = [&](bool bLocal = true, bool bAuth = true, bool bOwned = true,
		bool bUIBusy = false, bool bAuthBusy = false, bool bSteamBusy = false, bool bFlowBusy = false,
		ERPGDungeonClientFlowState State = ERPGDungeonClientFlowState::InSession)
	{
		return Widget::BuildPresentation(bLocal, bAuth, bUIBusy, bAuthBusy, bSteamBusy, bFlowBusy,
			bOwned, Member.CharacterId, Session, State);
	};
	Session.State = TEXT("Waiting");
	TestFalse(TEXT("Waiting cannot enter despite an address"), View().bCanEnter);
	TestTrue(TEXT("Waiting can leave"), View().bCanLeave);
	TestFalse(TEXT("Active character cannot switch"), View().bCanSelectCharacter);
	TestFalse(TEXT("Active session cannot create another"), View().bCanCreateSession);
	TestFalse(TEXT("Active session cannot create a character"), View().bCanCreateCharacter);
	Session.State = TEXT("Loading");
	TestFalse(TEXT("Allocator assignment is not server readiness"), View().bCanEnter);
	TestFalse(TEXT("Loading cannot abandon through lobby"), View().bCanLeave);
	Session.State = TEXT("InProgress");
	TestTrue(TEXT("Authenticated member of a ready server may enter"), View().bCanEnter);
	const auto DeniedView = Widget::BuildPresentation(true, true, false, false, false, false,
		true, Member.CharacterId, Session, ERPGDungeonClientFlowState::Error, TEXT("dungeon_reconnect_grace_expired"));
	TestFalse(TEXT("Cached expiry disables entry even with an old InProgress snapshot"), DeniedView.bCanEnter);
	TestTrue(TEXT("Cached expiry still offers authenticated membership refresh"), DeniedView.bCanRefresh && DeniedView.bCanResume);
	TestFalse(TEXT("Denial alone does not permit new sessions or character switching"), DeniedView.bCanCreateSession || DeniedView.bCanSelectCharacter);
	TestFalse(TEXT("Non-lobby widgets cannot enter"), View(false).bCanEnter);
	TestFalse(TEXT("Unauthenticated widgets cannot enter"), View(true, false).bCanEnter);
	TestFalse(TEXT("Selected character must belong to loaded roster"), View(true, true, false).bCanEnter);
	TestFalse(TEXT("UI requests lock entry"), View(true, true, true, true).bCanEnter);
	TestFalse(TEXT("Auth requests lock entry"), View(true, true, true, false, true).bCanEnter);
	TestFalse(TEXT("Steam requests lock entry"), View(true, true, true, false, false, true).bCanEnter);
	TestFalse(TEXT("Flow requests lock entry"), View(true, true, true, false, false, false, true).bCanEnter);
	for (const auto State : {ERPGDungeonClientFlowState::Connecting, ERPGDungeonClientFlowState::ReturningToLobby})
		TestFalse(TEXT("Travel guards remain after the HTTP request completes"), View(true, true, true, false, false, false, false, State).bCanEnter);
	Session.Members[0].CharacterId = TEXT("Owned-character");
	TestFalse(TEXT("Membership is case-sensitive"), View().bCanEnter);
	Session.Members[0] = Member;
	Session.ServerAddress.Reset();
	TestFalse(TEXT("Missing trusted address blocks entry"), View().bCanEnter);
	Session.ServerAddress = TEXT("127.0.0.1:18827");
	Session.ServerId.Reset();
	TestFalse(TEXT("Missing server assignment blocks entry"), View().bCanEnter);
	Session.ServerId = TEXT("assigned-server");
	for (const FString State : {TEXT("inprogress"), TEXT("SettlementPending"), TEXT("Unknown")})
	{
		Session.State = State;
		TestFalse(TEXT("Unknown or unfinished states cannot enter"), View().bCanEnter);
		TestFalse(TEXT("Unrecognized active snapshots do not authorize new sessions"), View().bCanCreateSession);
		TestFalse(TEXT("Unrecognized active snapshots do not authorize character creation"), View().bCanCreateCharacter);
	}
	for (const FString State : {TEXT("Cleared"), TEXT("Failed"), TEXT("Closed")})
	{
		Session.State = State;
		TestTrue(TEXT("Terminal session allows another dungeon"), View().bCanCreateSession);
		TestTrue(TEXT("Terminal session allows character change"), View().bCanSelectCharacter);
		TestTrue(TEXT("Terminal session allows character creation"), View().bCanCreateCharacter);
		TestFalse(TEXT("Completed server cannot be entered"), View().bCanEnter);
	}
	Session = FRPGDungeonSession();
	TestTrue(TEXT("An empty session may create a new one"), View().bCanCreateSession);
	TestTrue(TEXT("An empty session may resume"), View().bCanResume);
	TestTrue(TEXT("An empty roster may create its first character"), View(true, true, false).bCanCreateCharacter);
	TestFalse(TEXT("Busy character creation is locked"), View(true, true, false, true).bCanCreateCharacter);
	TestTrue(TEXT("Unauthenticated lobby offers login"), View(true, false).bCanLogin);
	TestFalse(TEXT("A non-lobby cannot log in through this UI"), View(false, false).bCanLogin);
	TestFalse(TEXT("Unauthenticated creation is locked"), View(true, false).bCanCreateSession);
	TestFalse(TEXT("Unauthenticated character creation is locked"), View(true, false).bCanCreateCharacter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyCharacterNameTest, "ProjectRPG.Online.DungeonLobbyCharacterName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGOnlineLobbyCharacterNameTest::RunTest(const FString&)
{
	FString Name;
	TestTrue(TEXT("Character names are trimmed"), URPGOnlineLobbyWidget::TryNormalizeCharacterName(TEXT("  NewHero  "), Name));
	TestEqual(TEXT("Only the normalized name is submitted"), Name, FString(TEXT("NewHero")));
	for (const FString Input : {FString(TEXT("ab")), FString::ChrN(20, TEXT('a')), FString(TEXT("한글 이름"))})
		TestTrue(TEXT("Valid boundary and Korean names are accepted"), URPGOnlineLobbyWidget::TryNormalizeCharacterName(Input, Name));
	TArray<FString> InvalidNames = {TEXT(""), TEXT(" \t "), TEXT("a"), FString::ChrN(21, TEXT('a')),
		TEXT("a\nb"), TEXT("a\tb")};
	for (TCHAR Control : {TCHAR(0), TCHAR(0x1f), TCHAR(0x7f), TCHAR(0x85), TCHAR(0x9f)})
	{
		FString Input(TEXT("ab"));
		// FString::InsertAt deliberately ignores NUL; build that malformed fixture explicitly.
		Input.GetCharArray().Insert(Control, 1);
		TestEqual(TEXT("The invalid fixture actually contains the control character"), Input.Len(), 3);
		InvalidNames.Add(MoveTemp(Input));
	}
	for (const FString& Input : InvalidNames)
	{
		Name = TEXT("previous-valid-name");
		TestFalse(TEXT("Invalid names are rejected before HTTP"), URPGOnlineLobbyWidget::TryNormalizeCharacterName(Input, Name));
		TestTrue(TEXT("Rejected normalization cannot retain a valid prior output"), Name.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyWidgetTest, "ProjectRPG.Online.DungeonLobbyWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGOnlineLobbyWidgetTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Lobby widget world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGOnlineLobbyWidget>(World, URPGOnlineLobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("Native lobby widget"), Widget)) return false;
	TArray<UButton*> Actions;
	UComboBoxString* DifficultyBox = nullptr;
	{
		const auto Slate = Widget->TakeWidget();
		TestFalse(TEXT("Unbound status is populated"), Widget->GetDisplayedStatus().IsEmpty());
		auto* Header = Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("OnlineLobbyPinnedHeader")));
		auto* Scroll = Cast<UScrollBox>(Widget->GetWidgetFromName(TEXT("OnlineLobbyContentScroll")));
		auto* Status = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("OnlineLobbyStatus")));
		if (!TestNotNull(TEXT("Pinned lobby header"), Header)
			|| !TestNotNull(TEXT("Scrollable lobby sections"), Scroll)
			|| !TestNotNull(TEXT("Existing named status"), Status)) return false;
		TestTrue(TEXT("Status stays outside scrolling content"), Status->GetParent() == Header
			&& Header->GetParent() == Scroll->GetParent());
		auto* NextStep = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("OnlineLobbyNextStep")));
		if (!TestNotNull(TEXT("Pinned next-step guidance"), NextStep)) return false;
		TestTrue(TEXT("Guidance remains outside the content scroll"), NextStep->GetParent() == Header);
		TestTrue(TEXT("Native guidance and BP getter agree"), NextStep->GetText().EqualTo(Widget->GetNextStepText()));
		TestFalse(TEXT("Unavailable context still explains the next step"), Widget->GetNextStepText().IsEmpty());
		TestTrue(TEXT("No lobby context means no recommended target"), Widget->GetNextStepControlName().IsNone());
		const auto* ConfiguredClassName = GetDefault<URPGOnlineLobbyWidget>()->CharacterClassNames.Find(TEXT("RPGPlayerClassDefinition:GreatSwordWarrior"));
		TestTrue(TEXT("The shipped class-name configuration imports a nonempty display label"), ConfiguredClassName && !ConfiguredClassName->IsEmpty());
		auto* LoginContent = Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("OnlineLobbyLoginContent")));
		auto* LobbyContent = Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("OnlineLobbyAuthenticatedContent")));
		auto* CharacterContent = Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("OnlineLobbyCharacterContent")));
		auto* Sections = Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("OnlineLobbySections")));
		if (!TestNotNull(TEXT("Dedicated login page"), LoginContent)
			|| !TestNotNull(TEXT("Dedicated character page"), CharacterContent)
			|| !TestNotNull(TEXT("Preparation page"), LobbyContent)
			|| !TestNotNull(TEXT("Shared scroll sections"), Sections)) return false;
		TestEqual(TEXT("Entry starts on login"), LoginContent->GetVisibility(), ESlateVisibility::Visible);
		TestEqual(TEXT("Preparation stays hidden before authentication"), LobbyContent->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("Characters stay hidden before authentication"), CharacterContent->GetVisibility(), ESlateVisibility::Collapsed);
		TestTrue(TEXT("Preparation remains in the existing scroll container"), LobbyContent->GetParent() == Sections && Sections->GetParent() == Scroll);
		for (const FName Name : {TEXT("OnlineLobbyAccountSection"), TEXT("OnlineLobbyCharacterSection"),
			TEXT("OnlineLobbyDungeonSection"), TEXT("OnlineLobbySteamSection")})
		{
			auto* Section = Widget->GetWidgetFromName(Name);
			if (!TestNotNull(TEXT("Named workflow section"), Section)) return false;
			TestTrue(TEXT("Account, character and preparation sections use their own page"), Section->GetParent()
				== (Name == TEXT("OnlineLobbyAccountSection") ? Sections
					: Name == TEXT("OnlineLobbyCharacterSection") ? CharacterContent : LobbyContent));
		}
		auto* HiddenAbandon = Cast<UButton>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButton")));
		auto* AbandonSize = HiddenAbandon ? Cast<USizeBox>(HiddenAbandon->GetParent()) : nullptr;
		if (!TestNotNull(TEXT("Abandonment layout wrapper"), AbandonSize)) return false;
		TestEqual(TEXT("Wrapper preserves existing development-only visibility"),
			AbandonSize->GetVisibility(), HiddenAbandon->GetVisibility());
		auto* AbandonReason = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("AbandonDungeonSessionButtonUnavailableReason")));
		if (!TestNotNull(TEXT("Abandonment unavailable-reason row"), AbandonReason)) return false;
		if (!HiddenAbandon->IsVisible())
		{
			TestTrue(TEXT("Hidden development actions do not leak reason text"),
				Widget->GetActionUnavailableReason(TEXT("AbandonDungeonSessionButton")).IsEmpty());
			TestEqual(TEXT("Hidden action reason occupies no layout space"), AbandonReason->GetVisibility(), ESlateVisibility::Collapsed);
		}
		TestTrue(TEXT("Unknown actions have no invented reason or authority"), Widget->GetActionUnavailableReason(TEXT("InventedAction")).IsEmpty());
		TestEqual(TEXT("Development login section uses the existing exposure gate"),
			Widget->GetWidgetFromName(TEXT("OnlineLobbyDevelopmentLoginSection")) != nullptr,
			URPGOnlineLobbyWidget::IsDevelopmentUIEnabled());
		TestEqual(TEXT("Development session section uses the existing exposure gate"),
			Widget->GetWidgetFromName(TEXT("OnlineLobbyDevelopmentSessionSection")) != nullptr,
			URPGOnlineLobbyWidget::IsDevelopmentUIEnabled());
		TestNotNull(TEXT("Real character selector"), Widget->GetWidgetFromName(TEXT("OwnedCharacters")));
		TestNotNull(TEXT("Real character name input"), Widget->GetWidgetFromName(TEXT("NewCharacterName")));
		auto* CreationClass = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("CharacterCreationClass")));
		if (!TestNotNull(TEXT("Server-provided creation class selector"), CreationClass)) return false;
		TestTrue(TEXT("Creation class selection handler is bound"), CreationClass->OnSelectionChanged.IsBound());
		TestTrue(TEXT("No catalog means no fabricated choice"), Widget->GetSelectedCreationClass() == nullptr && CreationClass->GetOptionCount() == 0);
		DifficultyBox = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonDifficulty")));
		if (!TestNotNull(TEXT("Real difficulty selector"), DifficultyBox)) return false;
		TestEqual(TEXT("Only four supported options"), DifficultyBox->GetOptionCount(), 4);
		TestTrue(TEXT("Difficulty dispatcher is bound"), DifficultyBox->OnSelectionChanged.IsBound());
		TestFalse(TEXT("No local lobby/auth context locks difficulty"), DifficultyBox->GetIsEnabled());
		ERPGGameDifficulty Before = ERPGGameDifficulty::Easy;
		if (!TestTrue(TEXT("Default selection is valid"), Widget->GetSelectedDungeonDifficulty(Before))) return false;
		const FString BeforeName = DifficultyBox->GetSelectedOption();
		DifficultyBox->SetSelectedOption(Before == ERPGGameDifficulty::Normal ? TEXT("Hell") : TEXT("Normal"));
		TestEqual(TEXT("Forced disabled selection restores the trusted display"), DifficultyBox->GetSelectedOption(), BeforeName);
		TestFalse(TEXT("Native/BP selector cannot bypass context"), Widget->SelectDungeonDifficulty(ERPGGameDifficulty::Easy));
		TestFalse(TEXT("Native/BP create cannot bypass context"), Widget->CreateSelectedDungeonSession());
		TestTrue(TEXT("Only loaded roster characters produce options"), Widget->GetCharacterOption(TEXT("invented-id")).IsEmpty());
		for (const FName Name : {TEXT("LoginSteamButton"), TEXT("LogoutBackendButton"), TEXT("ReloadCharactersButton"), TEXT("CreateCharacterButton"),
			TEXT("ContinueWithCharacterButton"), TEXT("ChangeCharacterButton"), TEXT("ReloadCreationOptionsButton"),
			TEXT("CreateDungeonSessionButton"), TEXT("ResumeDungeonSessionButton"), TEXT("RefreshDungeonSessionButton"),
			TEXT("EnterDungeonButton"), TEXT("LeaveDungeonSessionButton"), TEXT("CreateSteamPartyButton"),
			TEXT("FindSteamPartiesButton"), TEXT("JoinSteamPartyButton"), TEXT("AcceptSteamInviteButton"),
			TEXT("InviteSteamFriendsButton"), TEXT("PublishSteamServerButton"), TEXT("LeaveSteamPartyButton")})
		{
			auto* Button = Cast<UButton>(Widget->GetWidgetFromName(Name));
			if (!TestNotNull(TEXT("Expected native action"), Button)) return false;
			auto* Size = Cast<USizeBox>(Button->GetParent());
			if (!TestNotNull(TEXT("Action has a minimum-height wrapper"), Size)) return false;
			TestEqual(TEXT("Minimum action height is not a fixed text clipping height"), Size->GetMinDesiredHeight(), 44.f);
			TestNotNull(TEXT("Direct text content remains compatible with retry labels"), Cast<UTextBlock>(Button->GetContent()));
			Actions.Add(Button);
			TestTrue(TEXT("The actual dispatcher is bound"), Button->OnClicked.IsBound());
			TestFalse(TEXT("No local lobby/auth context means locked actions"), Button->GetIsEnabled());
			const auto Reason = Widget->GetActionUnavailableReason(Name);
			auto* ReasonRow = Cast<UTextBlock>(Widget->GetWidgetFromName(FName(*(Name.ToString() + TEXT("UnavailableReason")))));
			if (!TestNotNull(TEXT("Disabled action has a readable sibling reason"), ReasonRow)) return false;
			const bool bVisibleAction = Name == TEXT("LoginSteamButton") && !URPGOnlineLobbyWidget::IsDevelopmentUIEnabled();
			TestEqual(TEXT("Only the visible login action exposes its reason at entry"), !Reason.IsEmpty(), bVisibleAction);
			TestTrue(TEXT("Native label and read-only query agree"), ReasonRow->GetText().EqualTo(Reason));
			TestEqual(TEXT("Hidden page reasons occupy no space"), ReasonRow->GetVisibility(),
				bVisibleAction ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			Button->OnClicked.Broadcast(); // must not bypass context or make any HTTP/Steam request
		}
		TestEqual(TEXT("Development login is explicit and absent by default"),
			Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton")) != nullptr, URPGOnlineLobbyWidget::IsDevelopmentUIEnabled());
		// Change presentation credentials only. No subsystem initialization,
		// authentication HTTP, character creation, session or travel is performed.
		auto* EntryGameInstance = NewObject<UGameInstance>(GEngine);
		Widget->Web = NewObject<UHttpWebManager>(EntryGameInstance);
		auto* CharacterInput = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("NewCharacterName")));
		CharacterInput->SetText(FText::FromString(TEXT("Retained draft")));
		auto* Configuration = Widget->GetCharacterConfigurationWidget();
		Widget->Web->SetAccessToken(TEXT("entry-presentation-fixture"));
		Widget->RefreshPresentation();
		TestEqual(TEXT("Authentication waits for server selection"), CharacterContent->GetVisibility(), ESlateVisibility::Collapsed);
		Widget->SelectServerPage();
		TestEqual(TEXT("Server choice opens character selection"), CharacterContent->GetVisibility(), ESlateVisibility::Visible);
		TestEqual(TEXT("Authentication alone does not open preparation"), LobbyContent->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("Authentication hides login"), LoginContent->GetVisibility(), ESlateVisibility::Collapsed);
		Widget->Flow = NewObject<URPGDungeonSessionSubsystem>(EntryGameInstance);
		FRPGBackendCharacter FirstCharacter, SecondCharacter;
		FirstCharacter.CharacterId = TEXT("first-character"); FirstCharacter.Name = TEXT("First hero");
		FirstCharacter.ProgressionState = TEXT("Available"); FirstCharacter.ClassId = TEXT("RPGPlayerClassDefinition:GreatSwordWarrior");
		FirstCharacter.CharacterLevel = 4;
		SecondCharacter.CharacterId = TEXT("second-character"); SecondCharacter.Name = TEXT("Second hero");
		TestTrue(TEXT("Presentation-only flow can retain the first identity"), Widget->Flow->SelectCharacter(FirstCharacter.CharacterId));
		Widget->bLoadingCharacters = true;
		Widget->HandleCharacters({FirstCharacter, SecondCharacter}, true);
		Widget->CharacterClassNames.Add(FirstCharacter.ClassId, FText::FromString(TEXT("Fixture class")));
		Widget->RefreshPresentation();
		auto* Details = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("OnlineLobbyCharacterDetails")));
		if (!TestNotNull(TEXT("Selected character progression detail"), Details)) return false;
		TestTrue(TEXT("Details use the saved level and authored class label"), Details->GetText().ToString().Contains(TEXT("Fixture class"))
			&& Details->GetText().ToString().Contains(TEXT("4")));
		TestFalse(TEXT("A sibling without a summary does not inherit the selected class"), Widget->GetCharacterProgressionText(SecondCharacter).ToString().Contains(TEXT("Fixture class")));
		TestEqual(TEXT("Roster labels show the first name without an internal ID"), Widget->GetCharacterOption(FirstCharacter.CharacterId), FirstCharacter.Name);
		TestEqual(TEXT("Roster labels resolve the second identity independently"), Widget->GetCharacterOption(SecondCharacter.CharacterId), SecondCharacter.Name);
		TestEqual(TEXT("List loading does not implicitly open preparation"), LobbyContent->GetVisibility(), ESlateVisibility::Collapsed);
		auto* Continue = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ContinueWithCharacterButton")));
		Continue->OnClicked.Broadcast();
		TestFalse(TEXT("Forced continue cannot bypass missing lobby/auth context"), Widget->bPreparationOpen);
		Widget->bPreparationOpen = true;
		Widget->RefreshPresentation();
		TestEqual(TEXT("Accepted navigation opens preparation for the selected identity"), LobbyContent->GetVisibility(), ESlateVisibility::Visible);
		TestEqual(TEXT("Preparation hides the character page"), CharacterContent->GetVisibility(), ESlateVisibility::Collapsed);
		Widget->Web->ClearAccessToken();
		Widget->RefreshPresentation();
		TestEqual(TEXT("Invalidated authentication returns to login"), LoginContent->GetVisibility(), ESlateVisibility::Visible);
		TestEqual(TEXT("Invalidated authentication hides preparation"), LobbyContent->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("Invalidated authentication preserves selected identity"), Widget->Flow->GetSelectedCharacterId(), FirstCharacter.CharacterId);
		Widget->Web->SetAccessToken(TEXT("renewed-entry-presentation-fixture"));
		Widget->RefreshPresentation();
		TestEqual(TEXT("Reauthentication restores the same preparation page"), LobbyContent->GetVisibility(), ESlateVisibility::Visible);
		TestTrue(TEXT("Page transitions preserve the configuration widget"), Configuration == Widget->GetCharacterConfigurationWidget());
		TestEqual(TEXT("Page transitions retain character draft input"), CharacterInput->GetText().ToString(), FString(TEXT("Retained draft")));
		Widget->Web = nullptr;
		Widget->Flow = nullptr;
		FRPGCharacterCreationOption Choice;
		Choice.ClassId = TEXT("RPGPlayerClassDefinition:FixtureCreation"); Choice.DisplayName = TEXT("Fixture class"); Choice.Description = TEXT("Fixture description");
		Widget->bLoadingCreationOptions = true;
		Widget->HandleCreationOptions({Choice}, true);
		TestTrue(TEXT("The sole server recipe is displayed and selected"), Widget->GetSelectedCreationClass()
			&& Widget->GetSelectedCreationClass()->ClassId == Choice.ClassId);
		FRPGCharacterCreationOption OtherChoice = Choice;
		OtherChoice.ClassId = TEXT("RPGPlayerClassDefinition:OtherFixture"); OtherChoice.DisplayName = TEXT("Other fixture");
		Widget->bLoadingCreationOptions = true;
		Widget->HandleCreationOptions({OtherChoice}, true);
		Widget->bLoadingCreationOptions = true;
		Widget->HandleCreationOptions({Choice, OtherChoice}, true);
		TestTrue(TEXT("A refreshed catalog preserves a still-supported ID"), Widget->GetSelectedCreationClass()
			&& Widget->GetSelectedCreationClass()->ClassId == OtherChoice.ClassId);
		Widget->bLoadingCreationOptions = true;
		Widget->HandleCreationOptions({}, false);
		TestTrue(TEXT("Failed refresh clears selectable recipes"), !Widget->GetSelectedCreationClass() && CreationClass->GetOptionCount() == 0);
		Widget->bLoadingCreationOptions = true;
		Widget->HandleCreationOptions({Choice, OtherChoice}, true);
		TestTrue(TEXT("Multiple newly offered recipes require an explicit selection"), !Widget->GetSelectedCreationClass());
		Widget->bLoadingCreationOptions = true;
		Widget->HandleCreationOptions({}, true);
		TestTrue(TEXT("A valid empty catalog offers no fallback class"), !Widget->GetSelectedCreationClass());
	}
	for (auto* Action : Actions) TestFalse(TEXT("Teardown removes all click handlers"), Action->OnClicked.IsBound());
	TestFalse(TEXT("Teardown removes difficulty handler"), DifficultyBox->OnSelectionChanged.IsBound());
	{
		const auto Slate = Widget->TakeWidget();
		for (auto* Action : Actions) TestTrue(TEXT("Reconstruction restores handlers"), Action->OnClicked.IsBound());
		TestTrue(TEXT("Reconstruction restores difficulty handler"), DifficultyBox->OnSelectionChanged.IsBound());
	}
	for (auto* Action : Actions) TestFalse(TEXT("Repeated teardown is safe"), Action->OnClicked.IsBound());
	TestFalse(TEXT("Repeated teardown releases difficulty handler"), DifficultyBox->OnSelectionChanged.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGCharacterCreationOptionsTest, "ProjectRPG.Online.DungeonCharacterCreationOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGCharacterCreationOptionsTest::RunTest(const FString&)
{
	const FString Choice = TEXT("{\"classId\":\"RPGPlayerClassDefinition:Fixture\",\"displayName\":\"Fixture class\",\"description\":\"Description\"}");
	TArray<FRPGCharacterCreationOption> Options;
	TestTrue(TEXT("A server-authored recipe decodes"), FRPGCharacterCreationOption::DecodeList(TEXT("[") + Choice + TEXT("]"), Options));
	TestTrue(TEXT("Decoded recipe carries its exact ID"), Options.Num() == 1 && Options[0].ClassId == TEXT("RPGPlayerClassDefinition:Fixture"));
	TestFalse(TEXT("Duplicate IDs or labels cannot make selection ambiguous"), FRPGCharacterCreationOption::DecodeList(TEXT("[") + Choice + TEXT(",") + Choice + TEXT("]"), Options));
	TestTrue(TEXT("Rejected catalog retains no partial selectable rows"), Options.IsEmpty());
	TestFalse(TEXT("A different asset type is not a creation class"), FRPGCharacterCreationOption::DecodeList(
		TEXT("[{\"classId\":\"OtherType:Fixture\",\"displayName\":\"Fixture\",\"description\":\"Description\"}]"), Options));
	TestTrue(TEXT("An empty supported list is distinct from a failed request"), FRPGCharacterCreationOption::DecodeList(TEXT("[]"), Options) && Options.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBackendCharacterSummaryTest, "ProjectRPG.Online.DungeonCharacterSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBackendCharacterSummaryTest::RunTest(const FString&)
{
	FRPGBackendCharacter Character;
	auto Decode = [&Character](const FString& Summary)
	{
		const FString Body = TEXT("{\"characterId\":\"own-character\",\"rosterId\":\"own-roster\",\"name\":\"Hero\",\"createdAt\":\"2026-10-26T00:00:00Z\"")
			+ Summary + TEXT("}");
		TSharedPtr<FJsonObject> Object;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) && FRPGBackendCharacter::Decode(Object, Character);
	};
	TestTrue(TEXT("An old server's character identity is still usable"), Decode(TEXT("")));
	TestTrue(TEXT("An absent summary never implies a starting level"), Character.CharacterLevel == 0 && Character.ClassId.IsEmpty());
	TestTrue(TEXT("A valid summary decodes"), Decode(TEXT(",\"progression\":{\"state\":\"Available\",\"classId\":\"RPGPlayerClassDefinition:GreatSwordWarrior\",\"level\":4}")));
	TestTrue(TEXT("Only validated metadata becomes displayable"), Character.ProgressionState == TEXT("Available")
		&& Character.CharacterLevel == 4 && Character.ClassId == TEXT("RPGPlayerClassDefinition:GreatSwordWarrior"));
	for (const FString Summary : {FString(TEXT(",\"progression\":null")),
		FString(TEXT(",\"progression\":{\"state\":\"Missing\",\"level\":1}")),
		FString(TEXT(",\"progression\":{\"state\":\"Available\",\"classId\":\"RPGPlayerClassDefinition:GreatSwordWarrior\",\"level\":1.5}")),
		FString(TEXT(",\"progression\":{\"state\":\"Available\",\"classId\":\"OtherType:Example\",\"level\":1}")),
		FString(TEXT(",\"progression\":{\"state\":\"Available\",\"classId\":\"RPGPlayerClassDefinition:GreatSwordWarrior\",\"level\":\"1\"}"))})
	{
		TestTrue(TEXT("Unavailable display metadata does not erase character identity"), Decode(Summary));
		TestTrue(TEXT("Missing or malformed summaries clear previous metadata"), Character.ProgressionState != TEXT("Available")
			&& Character.CharacterLevel == 0 && Character.ClassId.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyDifficultyBlueprintTest, "ProjectRPG.Online.DungeonLobbyDifficulty.BlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyDifficultyBlueprintTest::RunTest(const FString&)
{
	const TArray<ERPGGameDifficulty> Expected = {ERPGGameDifficulty::Easy, ERPGGameDifficulty::Normal,
		ERPGGameDifficulty::Hard, ERPGGameDifficulty::Hell};
	TestTrue(TEXT("Typed BP options retain the four backend difficulty values"),
		URPGOnlineLobbyWidget::GetDungeonDifficultyOptions() == Expected);
	for (const FName Name : {TEXT("GetDungeonDifficultyOptions"), TEXT("GetSelectedDungeonDifficulty"), TEXT("CanSelectDungeonDifficulty")})
	{
		const UFunction* Function = URPGOnlineLobbyWidget::StaticClass()->FindFunctionByName(Name);
		TestTrue(TEXT("BP can read difficulty without sending a request"), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
	}
	for (const FName Name : {TEXT("GetNextStepText"), TEXT("GetNextStepControlName"), TEXT("GetActionUnavailableReason")})
	{
		const auto* Function = URPGOnlineLobbyWidget::StaticClass()->FindFunctionByName(Name);
		TestTrue(TEXT("BP guidance is read-only and cannot dispatch an action"), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
	}
	for (const FName Name : {TEXT("SelectDungeonDifficulty"), TEXT("CreateSelectedDungeonSession")})
	{
		const UFunction* Function = URPGOnlineLobbyWidget::StaticClass()->FindFunctionByName(Name);
		TestTrue(TEXT("BP has guarded explicit selection/creation nodes"), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}
	const auto* Getter = URPGOnlineLobbySubsystem::StaticClass()->FindFunctionByName(TEXT("GetLobbyWidget"));
	TestTrue(TEXT("Custom BP can obtain the existing native frontend"), Getter && Getter->HasAnyFunctionFlags(FUNC_BlueprintPure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOnlineLobbyDifficultyDisplayTest, "ProjectRPG.Online.DungeonLobbyDifficulty.SessionDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOnlineLobbyDifficultyDisplayTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Difficulty widget world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGOnlineLobbyWidget>(World, URPGOnlineLobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("Difficulty widget"), Widget)) return false;
	const auto Slate = Widget->TakeWidget();
	auto* Box = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonDifficulty")));
	const auto* Preference = FindFProperty<FStrProperty>(Widget->GetClass(), TEXT("Difficulty"));
	const auto* FlowProperty = FindFProperty<FObjectPropertyBase>(Widget->GetClass(), TEXT("Flow"));
	const auto* SessionProperty = FindFProperty<FStructProperty>(URPGDungeonSessionSubsystem::StaticClass(), TEXT("CurrentDungeonSession"));
	if (!TestTrue(TEXT("Existing reflected state for read-only native fixture"), Box && Preference && FlowProperty && SessionProperty)) return false;
	Preference->SetPropertyValue_InContainer(Widget, TEXT("Hell"));
	UGameInstance* Instance = NewObject<UGameInstance>();
	auto* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	FlowProperty->SetObjectPropertyValue_InContainer(Widget, Flow);
	auto& Session = *SessionProperty->ContainerPtrToValuePtr<FRPGDungeonSession>(Flow);
	Session.DungeonSessionId = TEXT("fixture-session");
	Session.Difficulty = TEXT("Normal");
	for (const FString State : {TEXT("Waiting"), TEXT("Loading"), TEXT("InProgress"), TEXT("SettlementPending"), TEXT("Unknown")})
	{
		Session.State = State;
		Box->OnSelectionChanged.Broadcast(TEXT("Easy"), ESelectInfo::Direct);
		ERPGGameDifficulty Selected = ERPGGameDifficulty::Easy;
		TestTrue(TEXT("Unfinished session displays the backend value"), Widget->GetSelectedDungeonDifficulty(Selected) && Selected == ERPGGameDifficulty::Normal);
		TestEqual(TEXT("Forced dispatcher cannot change the displayed session"), Box->GetSelectedOption(), FString(TEXT("Normal")));
		TestFalse(TEXT("Session difficulty is immutable through the selector"), Widget->CanSelectDungeonDifficulty());
		TestEqual(TEXT("Frontend preference remains separate"), Preference->GetPropertyValue_InContainer(Widget), FString(TEXT("Hell")));
	}
	Session.Difficulty = TEXT("hard ");
	Box->OnSelectionChanged.Broadcast(TEXT("Easy"), ESelectInfo::Direct);
	ERPGGameDifficulty Selected = ERPGGameDifficulty::Easy;
	TestFalse(TEXT("Invalid backend value never becomes a default difficulty"), Widget->GetSelectedDungeonDifficulty(Selected));
	TestTrue(TEXT("Malformed backend value clears rather than defaults the selector"), Box->GetSelectedOption().IsEmpty());
	Session.State = TEXT("Cleared");
	TestTrue(TEXT("After completion the local next-dungeon preference is used"), Widget->GetSelectedDungeonDifficulty(Selected) && Selected == ERPGGameDifficulty::Hell);
	Session = FRPGDungeonSession();
	for (const FString Invalid : {TEXT(""), TEXT("hard"), TEXT("Hard "), TEXT("Nightmare"), TEXT("3")})
	{
		Preference->SetPropertyValue_InContainer(Widget, Invalid);
		Box->OnSelectionChanged.Broadcast(TEXT("Easy"), ESelectInfo::Direct);
		TestFalse(TEXT("Malformed config does not silently create Hard"), Widget->GetSelectedDungeonDifficulty(Selected));
		TestTrue(TEXT("Invalid configured selection is visibly empty"), Box->GetSelectedOption().IsEmpty());
		TestFalse(TEXT("Invalid enum cannot bypass creation context"), Widget->SelectDungeonDifficulty(static_cast<ERPGGameDifficulty>(255)));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlayerLogoutPolicyTest, "ProjectRPG.Online.DungeonLogout.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPlayerLogoutPolicyTest::RunTest(const FString&)
{
	using Lobby = URPGOnlineLobbySubsystem;
	TestTrue(TEXT("Idle local lobby with current/expired credential may logout"), Lobby::IsLogoutAllowed(true, true, false, false, false, false));
	TestFalse(TEXT("Dungeon/client/server/foreign lobby cannot logout"), Lobby::IsLogoutAllowed(false, true, false, false, false, false));
	TestFalse(TEXT("No credential needs no backend logout"), Lobby::IsLogoutAllowed(true, false, false, false, false, false));
	TestFalse(TEXT("Active dungeon membership must be resolved first"), Lobby::IsLogoutAllowed(true, true, true, false, false, false));
	TestFalse(TEXT("Uncertain abandonment cannot be discarded"), Lobby::IsLogoutAllowed(true, true, false, true, false, false));
	TestFalse(TEXT("Steam party must be left explicitly first"), Lobby::IsLogoutAllowed(true, true, false, false, true, false));
	TestFalse(TEXT("Login, frontend, Steam and travel work block logout"), Lobby::IsLogoutAllowed(true, true, false, false, false, true));
	return true;
}
#endif
