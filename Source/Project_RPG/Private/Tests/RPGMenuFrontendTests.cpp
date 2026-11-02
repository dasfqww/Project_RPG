#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"
#include "UI/Lobby/RPGTutorialEntryPolicy.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTutorialEntryPolicyTest, "ProjectRPG.Online.TutorialEntry.SessionDecisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTutorialEntryPolicyTest::RunTest(const FString&)
{
	using Action = ERPGTutorialEntryAction;
	FRPGDungeonSession S;
	const FString Character = TEXT("owned-character");
	auto Decide = [&]() { return FRPGTutorialEntryPolicy::Decide(S, Character, false, false, false); };
	TestEqual(TEXT("Only a resolved empty snapshot may create"), Decide(), Action::Create);
	TestEqual(TEXT("In-flight work cannot create again"), FRPGTutorialEntryPolicy::Decide(S, Character, true, false, false), Action::Wait);
	TestEqual(TEXT("An unresolved persisted command blocks a new create"), FRPGTutorialEntryPolicy::Decide(S, Character, false, true, false), Action::Reject);
	TestEqual(TEXT("Admission denial remains authoritative"), FRPGTutorialEntryPolicy::Decide(S, Character, false, false, true), Action::Reject);
	TestEqual(TEXT("Missing selected identity cannot create"), FRPGTutorialEntryPolicy::Decide(S, FString(), false, false, false), Action::Reject);
	S.State = TEXT("InProgress"); TestEqual(TEXT("An incomplete nonempty snapshot is rejected"), Decide(), Action::Reject);
	S.DungeonSessionId = TEXT("session"); S.DungeonId = TEXT("Progression.Training"); S.Difficulty = TEXT("Normal");
	S.State = TEXT("Waiting"); S.LeaderCharacterId = Character;
	FRPGDungeonSessionMember Member; Member.CharacterId = Character; S.Members.Add(Member);
	TestEqual(TEXT("Existing solo waiting session proceeds through the departure API"), Decide(), Action::Depart);
	S.DepartureRequestedAt = TEXT("server-receipt");
	TestEqual(TEXT("A recorded departure is never sent twice"), Decide(), Action::Wait);
	S.State = TEXT("Loading"); S.ServerId = TEXT("server"); S.ServerAddress = TEXT("127.0.0.1:18896");
	TestEqual(TEXT("An address is not server readiness"), Decide(), Action::Wait);
	S.State = TEXT("InProgress"); TestEqual(TEXT("Ready owned training may request the existing ticket"), Decide(), Action::Connect);
	S.ServerAddress.Reset(); TestEqual(TEXT("Missing ready endpoint waits"), Decide(), Action::Wait); S.ServerAddress = TEXT("127.0.0.1:18896");
	for (const TCHAR* State : {TEXT("Cleared"), TEXT("Failed"), TEXT("Closed"), TEXT("inprogress"), TEXT("Unknown")})
	{ S.State = State; TestEqual(TEXT("Terminal/unknown snapshots cannot silently create or reconnect"), Decide(), Action::Reject); }
	S.State = TEXT("InProgress"); S.DungeonId = TEXT("Dungeon.Other");
	TestEqual(TEXT("An active other dungeon is never replaced"), Decide(), Action::Reject); S.DungeonId = TEXT("Progression.Training");
	S.Difficulty = TEXT("Hard"); TestEqual(TEXT("An unsupported training difficulty is excluded"), Decide(), Action::Reject); S.Difficulty = TEXT("Normal");
	S.Members[0].CharacterId = TEXT("other-character"); TestEqual(TEXT("A foreign member is excluded"), Decide(), Action::Reject); S.Members[0] = Member;
	S.Members.Add(Member); TestEqual(TEXT("Solo training cannot absorb a party"), Decide(), Action::Reject);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntro1EntryPolicyTest, "ProjectRPG.Online.Intro1Entry.SessionDecisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGIntro1EntryPolicyTest::RunTest(const FString&)
{
	using Action = ERPGTutorialEntryAction;
	FRPGDungeonSession S;
	auto Decide = [&]() { return FRPGTutorialEntryPolicy::Decide(S, TEXT("hero"), false, false, false, TEXT("Progression.Intro1")); };
	TestEqual(TEXT("Resolved empty membership can create only the requested practice"), Decide(), Action::Create);
	TestEqual(TEXT("Unknown target never creates"), FRPGTutorialEntryPolicy::Decide(S, TEXT("hero"), false, false, false, TEXT("Progression.Intro2")), Action::Reject);
	S.DungeonSessionId = TEXT("session"); S.DungeonId = TEXT("Progression.Intro1"); S.Difficulty = TEXT("Normal");
	S.State = TEXT("Waiting"); S.LeaderCharacterId = TEXT("hero"); S.Members.AddDefaulted_GetRef().CharacterId = TEXT("hero");
	TestEqual(TEXT("Intro1 own waiting membership departs"), Decide(), Action::Depart);
	S.DungeonId = TEXT("Progression.Training"); TestEqual(TEXT("Captured Intro1 action cannot depart a training session"), Decide(), Action::Reject);
	S.DungeonId = TEXT("Progression.Intro1"); S.State = TEXT("Loading"); TestEqual(TEXT("Assignment waits without another create"), Decide(), Action::Wait);
	S.State = TEXT("InProgress"); S.ServerId = TEXT("assigned"); S.ServerAddress = TEXT("127.0.0.1:18905");
	TestEqual(TEXT("Only requested Intro1 membership connects"), Decide(), Action::Connect);
	TestEqual(TEXT("Default Training action cannot connect to Intro1"), FRPGTutorialEntryPolicy::Decide(S, TEXT("hero"), false, false, false), Action::Reject);
	TestEqual(TEXT("Admission denial still blocks Intro1"), FRPGTutorialEntryPolicy::Decide(S, TEXT("hero"), false, false, true, TEXT("Progression.Intro1")), Action::Reject);
	S.State = TEXT("SettlementPending"); TestEqual(TEXT("An unsettled session is never replaced"), Decide(), Action::Reject);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMenuFrontendTest, "ProjectRPG.Online.MenuFrontend.PagesAndAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMenuFrontendTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/MenuFrontend_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	UWorld* World = Instance->GetWorld();
	if (!TestNotNull(TEXT("Minimal menu world"), World)) return false;
	auto* Viewport = NewObject<UGameViewportClient>(GEngine);
	Instance->GetWorldContext()->GameViewport = Viewport;
	Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
	struct FRelease
	{
		UGameInstance* Instance; UGameViewportClient* Viewport; UWorld* World;
		~FRelease() { Instance->Shutdown(); Viewport->DetachViewportClient(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	} Release{Instance.Get(), Viewport, World};
	FString Error; auto* Player = Instance->CreateLocalPlayer(0, Error, false);
	if (!TestNotNull(TEXT("Local player"), Player)) return false;
	auto* PC = World->SpawnActor<APlayerController>(); PC->SetPlayer(Player); World->AddController(PC);
	auto* Lobby = Instance->GetSubsystem<URPGOnlineLobbySubsystem>();
	if (!TestNotNull(TEXT("Lobby subsystem"), Lobby)) return false;
	auto* MapProperty = FindFProperty<FStrProperty>(Lobby->GetClass(), TEXT("LobbyMapPackage"));
	if (!TestNotNull(TEXT("Lobby package property"), MapProperty)) return false;
	MapProperty->SetPropertyValue_InContainer(Lobby, World->GetOutermost()->GetName());
	TStrongObjectPtr<URPGOnlineLobbyWidget> Widget(CreateWidget<URPGOnlineLobbyWidget>(PC));
	if (!TestNotNull(TEXT("Menu widget"), Widget.Get())) return false;
	const auto Slate = Widget->TakeWidget();
	if (!TestNotNull(TEXT("Frontend auth dependency"), Widget->Web.Get())
		|| !TestNotNull(TEXT("Frontend flow dependency"), Widget->Flow.Get())
		|| !TestNotNull(TEXT("Server page"), Widget->ServerContent.Get())
		|| !TestNotNull(TEXT("Menu page"), Widget->MenuContent.Get())) return false;
	Widget->Web->SetAccessToken(TEXT("menu-presentation-fixture"));
	Widget->bInitialCharacterLoadPending = false; Widget->bHasLoadedCharacterList = true;
	Widget->bServerSelected = Widget->bPreparationOpen = false;
	Widget->RefreshPresentation();
	TestEqual(TEXT("Authenticated entry starts at server choice"), Widget->ServerContent->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Character page is not mixed into the server page"), Widget->CharacterContent->GetVisibility(), ESlateVisibility::Collapsed);
	Widget->SelectServerPage();
	TestEqual(TEXT("Server choice opens character selection"), Widget->CharacterContent->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Selected server page is removed"), Widget->ServerContent->GetVisibility(), ESlateVisibility::Collapsed);
	FRPGBackendCharacter Character; Character.CharacterId = TEXT("owned-character"); Character.Name = TEXT("Hero");
	Widget->OwnedCharacters = {Character};
	// A presentation fixture uses the existing selection API without a network
	// transport or automatic configuration reads.
	Widget->Flow = NewObject<URPGDungeonSessionSubsystem>(Instance.Get());
	auto* Configuration = Widget->ConfigurationPanel.Get();
	Widget->ConfigurationPanel = nullptr;
	if (!TestTrue(TEXT("Select fixture identity through the existing API"), Widget->Flow->SelectCharacter(Character.CharacterId))) return false;
	Widget->RefreshPresentation(); Widget->ContinueWithCharacter();
	TestEqual(TEXT("Connect opens a menu lobby"), Widget->MenuContent->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Skill editor does not dominate the main menu"), Widget->ConfigurationSection->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Protocol controls are outside the main lobby"), Widget->DungeonSection->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("An opaque presentation token cannot authenticate tutorial admission"), Widget->CanStartTutorial());
	TestTrue(TEXT("Main page does not issue a session creation"), Widget->Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty());
	Widget->ChangeCharacter();
	TestEqual(TEXT("Character change returns to its own page"), Widget->CharacterContent->GetVisibility(), ESlateVisibility::Visible);
	Widget->ShowServerPage();
	TestEqual(TEXT("Server back is UI navigation"), Widget->ServerContent->GetVisibility(), ESlateVisibility::Visible);
	TestEqual(TEXT("Navigation does not clear selected server-owned identity"), Widget->Flow->GetSelectedCharacterId(), Character.CharacterId);
	Widget->ConfigurationPanel = Configuration;
	if (!TestNotNull(TEXT("Existing configuration owner"), Configuration)) return false;
	const FDateTime Issued = FDateTime::UtcNow(); const double Now = FPlatformTime::Seconds();
	TestTrue(TEXT("Install presentation-only account binding without HTTP"), Widget->Web->PlayerAuth.AcceptLogin(
		TEXT("confirmed-progression-fixture"), TEXT("76561198000000001"),
		(Issued + FTimespan::FromMinutes(5)).ToIso8601(), Issued.ToIso8601(), Now, Now));
	Widget->bServerSelected = Widget->bPreparationOpen = true;
	Widget->RefreshPresentation();
	TestNull(TEXT("No record is not a completed tutorial"), Configuration->GetConfirmedProgression());
	FRPGCharacterProgressionSnapshot Completed;
	Completed.CharacterId = Character.CharacterId; Completed.ProfileId = TEXT("FirstSlice.GreatSword.v1");
	Completed.ClassId = FPrimaryAssetId(TEXT("RPGPlayerClassDefinition:GreatSwordWarrior"));
	Completed.Revision = 2; Completed.Experience = 100; Completed.Level = 2; Completed.TotalSP = 3;
	Completed.Milestones = 1; Completed.bIdentityUnlocked = true;
	for (const TCHAR* Name : {TEXT("Player.Ability.Skill.AssultBlade"), TEXT("Player.Ability.Skill.Charge"), TEXT("Player.Ability.Skill.WhirlWind")})
	{
		FRPGSavedProgressionSkill Skill; Skill.SkillTag = FGameplayTag::RequestGameplayTag(FName(Name));
		Skill.Level = 1; Skill.TripodIds = {FString(), FString()}; Completed.Skills.Add(Skill);
	}
	Completed.SkillSlots.SetNum(8); Completed.SkillSlots[0] = Completed.Skills[0].SkillTag; Completed.SkillSlots[1] = Completed.Skills[1].SkillTag;
	FRPGCharacterProgressionResult Confirmed; Confirmed.bSuccessful = true; Confirmed.Snapshot = Completed;
	Configuration->HandleResult(++Configuration->RequestSerial, false, Confirmed);
	TestNotNull(TEXT("A confirmed record is available only in its current context"), Configuration->GetConfirmedProgression());
	TestTrue(TEXT("Saved milestone completes the menu with no retained session"), Widget->HasCompletedTutorial());
	{
		const FString PreviousCommand = FCommandLine::Get();
		struct FRestoreCommand { FString Value; ~FRestoreCommand() { FCommandLine::Set(*Value); } } Restore{PreviousCommand};
		FCommandLine::Set(TEXT("-RPGDungeonProfile=Intro1 -RPGDevelopmentLogin -nosteam"));
		Widget->LoadDungeonDefinitions(); Widget->RefreshPresentation();
		TestEqual(TEXT("Confirmed training selects Intro1 in the menu"), Widget->GetPracticeEntryDungeonId(), FString(TEXT("Progression.Intro1")));
		auto* Entry = Cast<UButton>(Widget->GetWidgetFromName(TEXT("StartTutorialButton")));
		auto* Label = Entry ? Cast<UTextBlock>(Entry->GetContent()) : nullptr;
		TestTrue(TEXT("The same direct action now offers Intro1"), Label && Label->GetText().ToString().Contains(TEXT("입문 1 입장")));
		TestTrue(TEXT("Confirmed current character can request Intro1 through existing gates"), Widget->CanStartTutorial());
		Configuration->bNeedsReload = true;
		TestFalse(TEXT("Stale progression cannot authorize Intro1"), Widget->CanStartTutorial());
		Configuration->bNeedsReload = false;
	}
	TestTrue(TEXT("An acquired but unslotted skill enables explicit draft filling"), Configuration->CanEquipUnslottedSkills());
	TestEqual(TEXT("Exactly the unassigned acquired skill is added"), Configuration->EquipUnslottedSkills(), 1);
	TestEqual(TEXT("Existing slot zero is preserved"), Configuration->DraftSlots[0], Completed.SkillSlots[0]);
	TestEqual(TEXT("Existing slot one is preserved"), Configuration->DraftSlots[1], Completed.SkillSlots[1]);
	TestEqual(TEXT("New skill fills the first empty slot"), Configuration->DraftSlots[2], Completed.Skills[2].SkillTag);
	TestFalse(TEXT("No save or transport is sent by the draft action"), Configuration->IsRequestInFlight());
	TestFalse(TEXT("The confirmed backend slot is unchanged until save"), Configuration->GetConfirmedProgression()->SkillSlots[2].IsValid());
	TestEqual(TEXT("Slot filling spends no SP"), Configuration->UsedDraftSP(), 0);
	TestEqual(TEXT("Repeated filling makes no duplicate"), Configuration->EquipUnslottedSkills(), 0);
	TestTrue(TEXT("The player can cancel the staged assignment"), Configuration->ResetDraft());
	Configuration->DraftSlots.Init(FGameplayTag(), 8);
	Configuration->DraftSlots[0] = Completed.Skills[1].SkillTag;
	Configuration->DraftSlots[5] = Completed.Skills[0].SkillTag;
	TestEqual(TEXT("Custom occupied slots allow only the remaining acquired skill"), Configuration->EquipUnslottedSkills(), 1);
	TestEqual(TEXT("A customized slot zero is not replaced"), Configuration->DraftSlots[0], Completed.Skills[1].SkillTag);
	TestEqual(TEXT("A customized slot five is not replaced"), Configuration->DraftSlots[5], Completed.Skills[0].SkillTag);
	TestEqual(TEXT("Custom layouts use their first available slot"), Configuration->DraftSlots[1], Completed.Skills[2].SkillTag);
	Configuration->HandleResult(++Configuration->RequestSerial, false, Confirmed);
	Configuration->bRequestInFlight = true;
	TestEqual(TEXT("A pending request blocks forced filling"), Configuration->EquipUnslottedSkills(), 0);
	Configuration->bRequestInFlight = false;
	Configuration->DraftSkills[0].Level = 2;
	FRPGCharacterProgressionResult Unavailable; Unavailable.HttpStatus = 503;
	Configuration->HandleResult(++Configuration->RequestSerial, false, Unavailable);
	TestNull(TEXT("A failed fresh read cannot authorize entry from a cached record"), Configuration->GetConfirmedProgression());
	TestEqual(TEXT("Read failure preserves the player's draft"), Configuration->DraftSkills[0].Level, 2);
	TestEqual(TEXT("Read failure preserves the saved milestone as cached display data"), Configuration->Saved.Milestones, 1);
	const uint64 SerialBeforeReload = Configuration->RequestSerial;
	Widget->ReloadTutorialProgress();
	TestEqual(TEXT("Progress reload never discards an unsaved draft"), Configuration->RequestSerial, SerialBeforeReload);
	Configuration->HandleResult(++Configuration->RequestSerial, false, Confirmed);
	Configuration->bNeedsReload = true;
	TestNull(TEXT("A rejected/invalidated read is not readiness"), Configuration->GetConfirmedProgression());
	TestFalse(TEXT("Invalidated saved data cannot mark the menu complete"), Widget->HasCompletedTutorial());
	Configuration->bNeedsReload = false; Configuration->bRequestInFlight = true;
	TestNull(TEXT("A pending request cannot authorize tutorial entry"), Configuration->GetConfirmedProgression());
	Configuration->bRequestInFlight = false;
	Configuration->Saved.CharacterId = TEXT("different-character");
	TestNull(TEXT("A foreign saved record cannot leak across selection"), Configuration->GetConfirmedProgression());
	Configuration->Saved = Completed;
	FRPGBackendCharacter Sibling; Sibling.CharacterId = TEXT("sibling-character"); Sibling.Name = TEXT("Sibling");
	Widget->OwnedCharacters.Add(Sibling); Widget->Flow->SelectCharacter(Sibling.CharacterId); Widget->RefreshPresentation();
	TestNull(TEXT("Character change clears confirmed readiness"), Configuration->GetConfirmedProgression());
	TestFalse(TEXT("A sibling cannot inherit completion"), Widget->HasCompletedTutorial());
	Widget->Flow->SelectCharacter(Character.CharacterId); Widget->RefreshPresentation();
	TestNull(TEXT("Returning to the first character requires its own read"), Configuration->GetConfirmedProgression());
	Configuration->HandleResult(++Configuration->RequestSerial, false, Confirmed);
	TestTrue(TEXT("Reselected character restores its stored completion"), Widget->HasCompletedTutorial());
	Widget->Web->ClearAccessToken();
	TestEqual(TEXT("Expired auth blocks forced filling"), Configuration->EquipUnslottedSkills(), 0);
	TestNull(TEXT("Expired credentials cannot expose a usable confirmed view"), Configuration->GetConfirmedProgression());
	Widget->ReleaseSlateResources(true);
	return true;
}
#endif
